/*
 * mish.c — main shell loop and command executor.
 *
 * The shell:
 *   - prints the prompt and reads a line with getline()
 *   - tokenizes it into a flat token stream
 *   - parses tokens into an array of Command structs (one per |/&/;)
 *   - executes each pipeline group as a unit
 *   - reaps any background children non-blockingly via SIGCHLD
 *
 * Pipeline execution supports arbitrary-length pipes.  A pipeline
 * group is a maximal run of commands joined by `|`, terminated by `;`,
 * `&`, or end-of-line.  All processes in the pipeline are forked first
 * with their pipe fds wired up; only then does the shell wait (or not,
 * if the group ended with `&`).
 *
 * Signal policy:
 *   SIGINT, SIGQUIT, SIGTSTP — ignored by the shell (so Ctrl-C/Z don't
 *     kill the shell itself).  Children re-enable defaults after fork.
 *   SIGCHLD — reaps background children with WNOHANG so we don't leave
 *     zombies around.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "token.h"
#include "command.h"
#include "builtin.h"

/* Global prompt string — `prompt` builtin replaces this.  Includes a
 * trailing space so the user doesn't have to.  Heap-allocated so it can
 * be freed and replaced. */
char *g_prompt = NULL;

/* ------------------------------------------------------------------------
 * Signal handlers
 * --------------------------------------------------------------------- */
static void sigchld_reaper(int sig)
{
    int saved_errno = errno;
    (void)sig;
    /* Reap as many dead children as are available.  WNOHANG → no block. */
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

static void install_signals(void)
{
    struct sigaction ign  = { .sa_handler = SIG_IGN };
    struct sigaction reap = { .sa_handler = sigchld_reaper };

    sigemptyset(&ign.sa_mask);
    sigemptyset(&reap.sa_mask);
    reap.sa_flags = SA_RESTART;

    sigaction(SIGINT,  &ign,  NULL);
    sigaction(SIGQUIT, &ign,  NULL);
    sigaction(SIGTSTP, &ign,  NULL);
    sigaction(SIGCHLD, &reap, NULL);
}

/* ------------------------------------------------------------------------
 * Apply a Command's redirections in the calling process.
 * Used inside the child after fork().
 * Returns 0 on success, -1 on error.
 * --------------------------------------------------------------------- */
static int apply_redirections(const Command *cmd)
{
    int fd;

    if (cmd->stdin_file != NULL) {
        fd = open(cmd->stdin_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "%s: %s\n", cmd->stdin_file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); return -1; }
        close(fd);
    }

    if (cmd->stdout_file != NULL) {
        int flags = O_WRONLY | O_CREAT | (cmd->append_stdout ? O_APPEND : O_TRUNC);
        fd = open(cmd->stdout_file, flags, 0666);
        if (fd < 0) {
            fprintf(stderr, "%s: %s\n", cmd->stdout_file, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); return -1; }
        close(fd);
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Run a single non-piped command (possibly a builtin, possibly external).
 * `bg` selects whether we wait for the child to finish.
 * Returns 1 if the shell should exit (only via the `exit` builtin).
 * --------------------------------------------------------------------- */
static int run_simple(Command *cmd, int bg)
{
    pid_t pid;

    /* Built-ins run in the shell process so cd, prompt, and exit can
     * actually take effect.  Built-ins don't honour `&` because they
     * always finish synchronously. */
    if (cmd->argv != NULL && cmd->argv[0] != NULL && builtin_is(cmd->argv[0])) {
        /* Save and restore stdin/stdout if the builtin has redirections. */
        int saved_in = -1, saved_out = -1;
        if (cmd->stdin_file != NULL || cmd->stdout_file != NULL) {
            saved_in  = dup(STDIN_FILENO);
            saved_out = dup(STDOUT_FILENO);
            if (apply_redirections(cmd) < 0) {
                if (saved_in  >= 0) { dup2(saved_in,  STDIN_FILENO);  close(saved_in);  }
                if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
                return 0;
            }
        }
        int rc = builtin_run(cmd);
        if (saved_in  >= 0) { dup2(saved_in,  STDIN_FILENO);  close(saved_in);  }
        if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
        return (rc == 1) ? 1 : 0;
    }

    if (cmd->argv == NULL || cmd->argv[0] == NULL)
        return 0;

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 0;
    }
    if (pid == 0) {
        /* Child: re-enable default signal handling so Ctrl-C kills the
         * child rather than being inherited as ignored. */
        signal(SIGINT,  SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        if (apply_redirections(cmd) < 0)
            _exit(1);

        execvp(cmd->argv[0], cmd->argv);
        fprintf(stderr, "%s: %s\n", cmd->argv[0], strerror(errno));
        _exit(127);
    }

    if (!bg) {
        int status;
        waitpid(pid, &status, 0);
    }
    /* Background: SIGCHLD handler reaps it later. */
    return 0;
}

/* ------------------------------------------------------------------------
 * Execute a pipeline group: commands[start..end-1] joined by SEP_PIPE,
 * with the final command's separator (`;` or `&`) determining whether
 * we wait.
 *
 * Strategy:
 *   1. Allocate (n-1) pipes.
 *   2. Fork n children, wiring each to the right pair of fds.
 *   3. Parent closes all pipe fds.
 *   4. If foreground, parent waits for the LAST pid (pipeline status =
 *      last command's status).  If background, parent doesn't wait —
 *      SIGCHLD reaper handles it.
 *
 * Note: built-ins inside a pipeline run in a forked child like any
 * external command.  This means `cd inside a pipeline` (which is
 * meaningless anyway) silently doesn't change the shell's cwd — the
 * standard POSIX shell behaviour.
 * --------------------------------------------------------------------- */
static int run_pipeline(Command *commands, int start, int end, int bg)
{
    int n = end - start;
    if (n < 1) return 0;
    if (n == 1)
        return run_simple(&commands[start], bg);

    int (*pipes)[2] = malloc((size_t)(n - 1) * sizeof(*pipes));
    pid_t *pids     = malloc((size_t)n * sizeof(*pids));
    if (pipes == NULL || pids == NULL) {
        perror("malloc");
        free(pipes); free(pids);
        return 0;
    }

    int i;
    for (i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            /* Close any already-opened pipe fds */
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            free(pipes); free(pids);
            return 0;
        }
    }

    for (i = 0; i < n; i++) {
        Command *cmd = &commands[start + i];
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            /* Best-effort cleanup of fds; surviving children will get
             * EOF on their pipe ends and exit naturally. */
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            free(pipes); free(pids);
            return 0;
        }

        if (pids[i] == 0) {
            /* Child */
            signal(SIGINT,  SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            /* stdin: read end of previous pipe (if not first cmd) */
            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);
            /* stdout: write end of next pipe (if not last cmd) */
            if (i < n - 1)
                dup2(pipes[i][1], STDOUT_FILENO);

            /* Close every pipe fd in the child — both the duped ones
             * and the others.  Leftover fds keep readers blocked. */
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Per-command file redirections override pipe wiring. */
            if (apply_redirections(cmd) < 0)
                _exit(1);

            if (cmd->argv != NULL && cmd->argv[0] != NULL
                && builtin_is(cmd->argv[0])) {
                /* Builtin in a pipeline: run it in the child. */
                int rc = builtin_run(cmd);
                _exit(rc < 0 ? 1 : 0);
            }

            if (cmd->argv == NULL || cmd->argv[0] == NULL)
                _exit(0);

            execvp(cmd->argv[0], cmd->argv);
            fprintf(stderr, "%s: %s\n", cmd->argv[0], strerror(errno));
            _exit(127);
        }
    }

    /* Parent: close all pipe fds */
    for (i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (!bg) {
        int status;
        for (i = 0; i < n; i++)
            waitpid(pids[i], &status, 0);
    }

    free(pipes);
    free(pids);
    return 0;
}

/* ------------------------------------------------------------------------
 * Walk the command array, grouping consecutive `|` segments and dispatching
 * them as pipelines.
 * --------------------------------------------------------------------- */
static int execute_all(Command *commands, int n)
{
    int i = 0;
    int should_exit = 0;

    while (i < n) {
        int j = i;
        /* Extend pipeline group while sep == "|" */
        while (j < n - 1 && strcmp(commands[j].sep, SEP_PIPE) == 0)
            j++;

        /* commands[i..j] is one pipeline group; commands[j].sep is its
         * terminating separator (";" or "&"). */
        int bg = (strcmp(commands[j].sep, SEP_BG) == 0);
        int rc = run_pipeline(commands, i, j + 1, bg);
        if (rc == 1) { should_exit = 1; break; }

        i = j + 1;
    }
    return should_exit;
}

/* ------------------------------------------------------------------------
 * REPL
 * --------------------------------------------------------------------- */
int main(void)
{
    char   *line   = NULL;
    size_t  cap    = 0;
    ssize_t n;
    int     done   = 0;

    install_signals();

    g_prompt = strdup("% ");
    if (g_prompt == NULL) { perror("strdup"); return 1; }

    while (!done) {
        fputs(g_prompt, stdout);
        fflush(stdout);

        n = getline(&line, &cap, stdin);
        if (n < 0) {
            if (errno == EINTR) { clearerr(stdin); continue; }
            putchar('\n');
            break;  /* EOF */
        }

        /* Strip trailing newline so the tokenizer doesn't see it. */
        if (n > 0 && line[n - 1] == '\n')
            line[n - 1] = '\0';

        /* Need a writable copy for the tokenizer because it splices NULs. */
        char *work = strdup(line);
        if (work == NULL) { perror("strdup"); continue; }

        char *tokens[MAX_TOKENS];
        int   nt = tokenize(work, tokens);
        if (nt < 0) {
            fprintf(stderr, "mish: too many tokens\n");
            free(work);
            continue;
        }
        if (nt == 0) { free(work); continue; }

        Command commands[MAX_COMMANDS];
        int nc = parse_commands(tokens, commands);
        if (nc < 0) {
            const char *msg = "syntax error";
            if (nc == -2) msg = "syntax error: missing command after pipe";
            if (nc == -3) msg = "syntax error: missing redirection target";
            fprintf(stderr, "mish: %s\n", msg);
            free(work);
            continue;
        }

        done = execute_all(commands, nc);

        for (int k = 0; k < nc; k++)
            command_free(&commands[k]);
        free(work);
    }

    free(line);
    free(g_prompt);
    return 0;
}
