/*
 * shell.c  --  Mini-shell: REPL + AST evaluator + execution engine.
 *
 * Parsing is handled by the BNFC-generated psInput() (Parser.h).
 * This file walks the resulting AST and executes each job.
 *
 * Features implemented:
 *   - Simple commands             ls -l /tmp
 *   - Pipelines                   ls | grep c | wc -l
 *   - Input / output redirection  sort < in.txt > out.txt
 *   - Background jobs             sleep 5 &
 *   - Variable assignment         NAME=value
 *   - Variable expansion          echo $HOME
 *   - Quoted strings              echo "hello world"
 *   - Multiple jobs on one line   echo a ; echo b
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#include "shell.h"
#include "Parser.h"
#include "Absyn.h"

/* =========================================================================
 * Environment variable helpers
 * ========================================================================= */

/*
 * expand_atom: resolve an Atom to a heap-allocated C string.
 * AWord  -> strdup of the token
 * AVar   -> getenv(name) or "" if unset
 * AQuoted -> strdup of the string literal (quotes stripped by lexer)
 */
static char *expand_atom(Atom a)
{
    if (a == NULL) return strdup("");
    switch (a->kind) {
        case is_AWord:
            return strdup(a->u.aword_.word_);
        case is_AVar: {
            /* VarRef token starts with '$'; skip it. */
            const char *name = a->u.avar_.varref_ + 1;
            const char *val  = getenv(name);
            return strdup(val ? val : "");
        }
        case is_AQuoted:
            return strdup(a->u.aquoted_.string_);
    }
    return strdup("");
}

/* =========================================================================
 * Build a Command struct from a CommandPart AST node
 * ========================================================================= */

static void build_command(CommandPart cp, Command *cmd)
{
    ListAtom la;
    int i = 0;

    memset(cmd, 0, sizeof(*cmd));

    /* First atom is the command name / argv[0]. */
    cmd->argv[i] = expand_atom(cp->u.cmd_.atom_);
    cmd->name    = cmd->argv[i];
    i++;

    /* Remaining atoms are the arguments.
     * Skip atoms that expand to the empty string (undefined $VAR, etc.),
     * matching POSIX word-splitting behaviour. */
    for (la = cp->u.cmd_.listatom_; la != NULL; la = la->listatom_) {
        if (i >= MAX_ARGS - 1) {
            fprintf(stderr, "shell: too many arguments (max %d)\n", MAX_ARGS - 1);
            break;
        }
        char *val = expand_atom(la->atom_);
        if (*val == '\0' && la->atom_->kind == is_AVar) {
            free(val); /* discard empty variable expansions */
        } else {
            cmd->argv[i++] = val;
        }
    }

    cmd->argv[i] = NULL;
    cmd->argc    = i;
}

/* =========================================================================
 * Apply I/O redirection to a Command struct
 * ========================================================================= */

static void apply_redir(OptRedir redir, Command *cmd)
{
    if (redir == NULL) return;
    switch (redir->kind) {
        case is_NoRedir:
            break;
        case is_OutRedir:
            cmd->outfile = expand_atom(redir->u.outredir_.atom_);
            break;
        case is_InRedir:
            cmd->infile = expand_atom(redir->u.inredir_.atom_);
            break;
        case is_InOutRedir:
            cmd->infile  = expand_atom(redir->u.inoutredir_.atom_1);
            cmd->outfile = expand_atom(redir->u.inoutredir_.atom_2);
            break;
        case is_OutInRedir:
            cmd->outfile = expand_atom(redir->u.outinredir_.atom_1);
            cmd->infile  = expand_atom(redir->u.outinredir_.atom_2);
            break;
    }
}

/* =========================================================================
 * Count pipeline depth
 * ========================================================================= */

static int pipeline_depth(Pipeline p)
{
    int n = 0;
    while (p != NULL) {
        n++;
        if (p->kind == is_Single) break;
        p = p->u.pipe_.pipeline_;
    }
    return n;
}

/* =========================================================================
 * execute_pipeline: fork children, wire pipes, exec each command
 * ========================================================================= */

void execute_pipeline(Command *cmds, int n, int background)
{
    int  i;
    int  pipes[n - 1][2];  /* n-1 pipes for n commands */
    pid_t pids[n];

    /* Create all pipes up front. */
    for (i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return;
        }
    }

    for (i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            return;
        }

        if (pids[i] == 0) {
            /* Child: wire stdin from previous pipe. */
            if (i > 0) {
                dup2(pipes[i - 1][0], STDIN_FILENO);
            }
            /* Wire stdout to next pipe. */
            if (i < n - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            /* Close all pipe fds in the child. */
            int j;
            for (j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Apply file redirections (only meaningful on first/last). */
            if (cmds[i].infile) {
                int fd = open(cmds[i].infile, O_RDONLY);
                if (fd < 0) { perror(cmds[i].infile); _exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (cmds[i].outfile) {
                int fd = open(cmds[i].outfile,
                              O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror(cmds[i].outfile); _exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            execvp(cmds[i].name, cmds[i].argv);
            fprintf(stderr, "shell: %s: %s\n", cmds[i].name, strerror(errno));
            _exit(127);
        }
    }

    /* Parent: close all pipe fds. */
    for (i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Wait for all children unless backgrounded. */
    if (!background) {
        for (i = 0; i < n; i++) {
            waitpid(pids[i], NULL, 0);
        }
    }
}

/* =========================================================================
 * execute_command: run a single command (no pipe)
 * ========================================================================= */

void execute_command(const Command *cmd)
{
    pid_t pid;
    int   fd;

    if (cmd == NULL || cmd->argc == 0) return;

    /* Built-in: cd */
    if (strcmp(cmd->name, "cd") == 0) {
        const char *dir = (cmd->argc > 1) ? cmd->argv[1] : getenv("HOME");
        if (!dir) dir = "/";
        if (chdir(dir) != 0) perror("cd");
        return;
    }

    /* Built-in: exit */
    if (strcmp(cmd->name, "exit") == 0) {
        int code = (cmd->argc > 1) ? atoi(cmd->argv[1]) : 0;
        exit(code);
    }

    /* Built-in: export NAME=VALUE */
    if (strcmp(cmd->name, "export") == 0) {
        int j;
        for (j = 1; j < cmd->argc; j++) {
            char *eq = strchr(cmd->argv[j], '=');
            if (eq) {
                char *name = strndup(cmd->argv[j], (size_t)(eq - cmd->argv[j]));
                setenv(name, eq + 1, 1);
                free(name);
            }
        }
        return;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        /* Apply I/O redirection. */
        if (cmd->infile) {
            fd = open(cmd->infile, O_RDONLY);
            if (fd < 0) { perror(cmd->infile); _exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (cmd->outfile) {
            fd = open(cmd->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror(cmd->outfile); _exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(cmd->name, cmd->argv);
        fprintf(stderr, "shell: %s: %s\n", cmd->name, strerror(errno));
        _exit(127);
    }

    if (!cmd->background) {
        waitpid(pid, NULL, 0);
    }
}

/* =========================================================================
 * is_assignment: detect and perform NAME=VALUE variable assignments.
 * Returns 1 if handled, 0 otherwise.
 * ========================================================================= */

static int try_assignment(const char *word)
{
    const char *eq = strchr(word, '=');
    if (!eq || eq == word) return 0;

    /* Verify that everything before '=' is a valid identifier. */
    const char *p;
    for (p = word; p != eq; p++) {
        if (!( (*p >= 'a' && *p <= 'z') ||
               (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9' && p != word) ||
               *p == '_' ))
            return 0;
    }

    /* setenv(name, value, overwrite) */
    char *name = strndup(word, (size_t)(eq - word));
    setenv(name, eq + 1, 1);
    free(name);
    return 1;
}

/* =========================================================================
 * eval_commandline: evaluate a parsed CommandLine AST node
 * ========================================================================= */

static void eval_commandline(CommandLine cl, int background)
{
    Pipeline  p       = cl->u.mkcmdline_.pipeline_;
    OptRedir  redir   = cl->u.mkcmdline_.optredir_;
    int       depth   = pipeline_depth(p);

    if (depth == 1) {
        /* Single command — check for variable assignment first. */
        CommandPart cp = p->u.single_.commandpart_;
        Command cmd;
        build_command(cp, &cmd);
        apply_redir(redir, &cmd);
        cmd.background = background;

        if (cmd.argc == 1 && try_assignment(cmd.name)) {
            /* Pure assignment: nothing else to run. */
            free(cmd.name);
            return;
        }

        execute_command(&cmd);

        /* Free expanded argument strings. */
        int i;
        for (i = 0; i < cmd.argc; i++) free(cmd.argv[i]);
        free(cmd.infile);
        free(cmd.outfile);
    } else {
        /* Pipeline: collect all CommandParts into an array. */
        Command cmds[depth];
        int     i = 0;
        Pipeline cur = p;

        while (cur != NULL) {
            CommandPart cp;
            if (cur->kind == is_Single) {
                cp = cur->u.single_.commandpart_;
                build_command(cp, &cmds[i]);
                /* Only apply file-level redir to the pipeline as a whole
                 * (first cmd gets infile, last gets outfile). */
                if (i == 0 && redir->kind != is_NoRedir) {
                    apply_redir(redir, &cmds[0]);
                }
                i++;
                break;
            } else {
                cp = cur->u.pipe_.commandpart_;
                build_command(cp, &cmds[i]);
                if (i == 0 && redir->kind != is_NoRedir) {
                    apply_redir(redir, &cmds[0]);
                }
                i++;
                cur = cur->u.pipe_.pipeline_;
            }
        }

        /* Last command gets outfile redirection. */
        if (i > 1 && redir->kind != is_NoRedir) {
            free(cmds[i - 1].outfile);
            cmds[i - 1].outfile = cmds[0].outfile ? strdup(cmds[0].outfile) : NULL;
            /* InRedir is only on cmd[0]; clear outfile from cmd[0] for pipeline. */
            if (redir->kind == is_OutRedir || redir->kind == is_OutInRedir) {
                free(cmds[0].outfile);
                cmds[0].outfile = NULL;
            }
        }

        execute_pipeline(cmds, i, background);

        /* Free all expanded argv strings. */
        int j, k;
        for (j = 0; j < i; j++) {
            for (k = 0; k < cmds[j].argc; k++) free(cmds[j].argv[k]);
            free(cmds[j].infile);
            free(cmds[j].outfile);
        }
    }
}

/* =========================================================================
 * eval_input: walk the top-level AST and run each job
 * ========================================================================= */

static void eval_input(Input tree)
{
    ListJob lj;
    if (tree == NULL) return;

    for (lj = tree->u.startinput_.listjob_; lj != NULL; lj = lj->listjob_) {
        Job j = lj->job_;
        switch (j->kind) {
            case is_OneJobFG:
                eval_commandline(j->u.onejobfg_.commandline_, 0);
                break;
            case is_OneJobBG:
                eval_commandline(j->u.onejobbg_.commandline_, 1);
                break;
        }
    }
}

/* =========================================================================
 * register_all_builtin_commands  (stub — extend in later lab)
 * ========================================================================= */

void register_all_builtin_commands(void)
{
    /*
     * Future: call register_hello_command(), register_ls_command(), etc.
     * once those modules are built from the PackageManagement chapter.
     */
}

/* =========================================================================
 * REPL — main entry point
 * ========================================================================= */

int main(void)
{
    char   *line = NULL;
    size_t  cap  = 0;
    ssize_t len;
    int     interactive = isatty(STDIN_FILENO);

    register_all_builtin_commands();

    while (1) {
        if (interactive) {
            printf("mini-shell> ");
            fflush(stdout);
        }

        len = getline(&line, &cap, stdin);
        if (len == -1) {
            if (interactive) printf("\n");
            break;
        }

        /* Strip trailing newline. */
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        /* Skip blank lines. */
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        /* Parse the line with the BNFC-generated parser. */
        Input tree = psInput(line);
        if (tree == NULL) {
            fprintf(stderr, "shell: parse error: %s\n", line);
            continue;
        }

        eval_input(tree);
        free_Input(tree);
    }

    free(line);
    return 0;
}
