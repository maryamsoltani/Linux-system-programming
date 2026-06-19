/*
 * aishell - BusyBox-style shell with 23 built-in commands,
 *           pipeline support, raw-mode line editing, tab completion,
 *           persistent history, and a natural-language @ interface.
 *
 * Shell name:    aishell
 * Version:       1.0.0
 * History file:  ~/.aishell_history
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "json_utils.h"

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define SHELL_NAME      "aishell"
#define SHELL_VERSION   "1.0.0"
#define HISTORY_FILE    ".aishell_history"
#define MAX_HISTORY     100
#define MAX_ARGS        256
#define MAX_LINE        4096
#define NL_CACHE_SIZE   64

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

void register_all_builtin_commands(void);

/* ------------------------------------------------------------------ */
/* History                                                              */
/* ------------------------------------------------------------------ */

static char *history[MAX_HISTORY];
static int   history_count = 0;

static void history_add(const char *line)
{
    if (line == NULL || line[0] == '\0') return;
    /* avoid duplicate consecutive entry */
    if (history_count > 0 &&
        strcmp(history[history_count - 1], line) == 0) return;

    if (history_count < MAX_HISTORY) {
        history[history_count++] = strdup(line);
    } else {
        free(history[0]);
        memmove(history, history + 1, (MAX_HISTORY - 1) * sizeof(char *));
        history[MAX_HISTORY - 1] = strdup(line);
    }
}

static void history_load(void)
{
    const char *home = getenv("HOME");
    if (!home) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", home, HISTORY_FILE);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] != '\0') history_add(line);
    }
    fclose(f);
}

static void history_save(void)
{
    const char *home = getenv("HOME");
    if (!home) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", home, HISTORY_FILE);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < history_count; i++)
        fprintf(f, "%s\n", history[i]);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* NL cache                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    char key[MAX_LINE];
    char value[MAX_LINE];
} nl_cache_entry_t;

static nl_cache_entry_t nl_cache[NL_CACHE_SIZE];
static int nl_cache_count = 0;

static void nl_normalize(const char *in, char *out, size_t outsz)
{
    size_t j = 0;
    int space = 0;
    for (size_t i = 0; in[i] && j + 1 < outsz; i++) {
        char c = (char)tolower((unsigned char)in[i]);
        if (isspace((unsigned char)c)) {
            if (!space && j > 0) { out[j++] = ' '; space = 1; }
        } else {
            out[j++] = c;
            space = 0;
        }
    }
    /* strip trailing space */
    while (j > 0 && out[j-1] == ' ') j--;
    out[j] = '\0';
}

static const char *nl_cache_lookup(const char *norm_key)
{
    for (int i = 0; i < nl_cache_count; i++) {
        if (strcmp(nl_cache[i].key, norm_key) == 0)
            return nl_cache[i].value;
    }
    return NULL;
}

static void nl_cache_store(const char *norm_key, const char *value)
{
    if (nl_cache_count < NL_CACHE_SIZE) {
        strncpy(nl_cache[nl_cache_count].key,   norm_key, MAX_LINE - 1);
        strncpy(nl_cache[nl_cache_count].value,  value,    MAX_LINE - 1);
        nl_cache_count++;
    } else {
        /* evict oldest */
        memmove(nl_cache, nl_cache + 1, (NL_CACHE_SIZE - 1) * sizeof(nl_cache_entry_t));
        strncpy(nl_cache[NL_CACHE_SIZE - 1].key,   norm_key, MAX_LINE - 1);
        strncpy(nl_cache[NL_CACHE_SIZE - 1].value,  value,    MAX_LINE - 1);
    }
}

/* ------------------------------------------------------------------ */
/* Command safety validation                                            */
/* ------------------------------------------------------------------ */

static int command_is_safe(const char *cmd)
{
    /* reject shell operators */
    const char *dangerous = ";|&<>`$()'\"\n\\";
    for (size_t i = 0; dangerous[i]; i++) {
        if (strchr(cmd, dangerous[i])) return 0;
    }

    /* get first token */
    char tmp[MAX_LINE];
    strncpy(tmp, cmd, MAX_LINE - 1);
    tmp[MAX_LINE - 1] = '\0';
    char *tok = strtok(tmp, " \t");
    if (!tok) return 0;

    /* allow registered commands */
    if (find_command(tok)) return 1;

    /* allow builtins */
    if (strcmp(tok, "help")  == 0) return 1;
    if (strcmp(tok, "exit")  == 0) return 1;
    if (strcmp(tok, "quit")  == 0) return 1;
    if (strcmp(tok, "version") == 0) return 1;

    return 0;
}

/* ------------------------------------------------------------------ */
/* Natural-language fallback                                            */
/* ------------------------------------------------------------------ */

static int fallback_nl_to_command(const char *input, char *out, size_t outsz)
{
    char norm[MAX_LINE];
    nl_normalize(input, norm, sizeof(norm));

    /* check cache first */
    const char *cached = nl_cache_lookup(norm);
    if (cached) {
        strncpy(out, cached, outsz - 1);
        out[outsz - 1] = '\0';
        return 1;
    }

    /* built-in heuristics */
    const char *result = NULL;

    if (strstr(norm, "list") != NULL || strstr(norm, "show files") != NULL ||
        strstr(norm, "what files") != NULL) {
        result = "ls";
    } else if (strstr(norm, "date") != NULL || strstr(norm, "today") != NULL) {
        result = "localdate";
    } else if (strstr(norm, "where am i") != NULL ||
               strstr(norm, "current directory") != NULL ||
               strstr(norm, "working dir") != NULL) {
        result = "pwd";
    } else if (strncmp(norm, "echo ", 5) == 0) {
        snprintf(out, outsz, "echo %s", input + 6);
        nl_cache_store(norm, out);
        return 1;
    } else if (strstr(norm, "who am i") != NULL || strstr(norm, "current user") != NULL) {
        result = "whoami";
    } else if (strstr(norm, "word count") != NULL || strstr(norm, "count words") != NULL ||
               strstr(norm, "count lines") != NULL) {
        result = "wc";
    } else if (strstr(norm, "create dir") != NULL || strstr(norm, "make dir") != NULL ||
               strstr(norm, "mkdir") != NULL) {
        result = "mkdir";
    } else if (strstr(norm, "remove dir") != NULL || strstr(norm, "delete dir") != NULL) {
        result = "rmdir";
    } else if (strstr(norm, "first lines") != NULL || strstr(norm, "top lines") != NULL ||
               strstr(norm, "beginning of") != NULL) {
        result = "head";
    } else if (strstr(norm, "last lines") != NULL || strstr(norm, "tail") != NULL ||
               strstr(norm, "end of") != NULL) {
        result = "tail";
    } else if (strstr(norm, "disk usage") != NULL || strstr(norm, "file size") != NULL) {
        result = "du";
    } else if (strstr(norm, "user id") != NULL || strstr(norm, "my id") != NULL) {
        result = "id";
    } else if (strstr(norm, "system info") != NULL || strstr(norm, "kernel") != NULL ||
               strstr(norm, "os info") != NULL) {
        result = "uname";
    } else if (strstr(norm, "clear screen") != NULL || strstr(norm, "clear terminal") != NULL) {
        result = "clear";
    } else if (strstr(norm, "help") != NULL) {
        result = "help";
    }

    if (result) {
        strncpy(out, result, outsz - 1);
        out[outsz - 1] = '\0';
        nl_cache_store(norm, out);
        return 1;
    }

    /* try external LLM helper */
    const char *llm = getenv("MYSH_LLM_HELPER");
    if (llm) {
        char cmd_buf[2048];
        snprintf(cmd_buf, sizeof(cmd_buf), "%s %s", llm, input);
        FILE *pipe = popen(cmd_buf, "r");
        if (pipe) {
            if (fgets(out, (int)outsz, pipe) != NULL) {
                out[strcspn(out, "\n")] = '\0';
                pclose(pipe);
                if (out[0] != '\0') {
                    nl_cache_store(norm, out);
                    return 1;
                }
            } else {
                pclose(pipe);
            }
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Tokenizer                                                            */
/* ------------------------------------------------------------------ */

static int tokenize(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p == '"') *p++ = '\0';
        } else if (*p == '\'') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '\'') p++;
            if (*p == '\'') *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

/* ------------------------------------------------------------------ */
/* Help command                                                          */
/* ------------------------------------------------------------------ */

static void list_cmd_cb(const cmd_spec_t *s, void *ud)
{
    (void)ud;
    printf("  %-16s %s\n", s->name, s->summary ? s->summary : "");
}

static void print_help_text(void)
{
    printf("%s %s - BusyBox-style shell\n\n", SHELL_NAME, SHELL_VERSION);
    printf("Built-in shell commands:\n");
    printf("  %-16s %s\n", "help",    "show this help");
    printf("  %-16s %s\n", "exit",    "exit the shell");
    printf("  %-16s %s\n", "quit",    "exit the shell");
    printf("  %-16s %s\n", "version", "show shell version");
    printf("\nRegistered commands:\n");
    for_each_command(list_cmd_cb, NULL);
    printf("\nType '<command> --help' for command-specific help.\n");
    printf("Use '@<natural language>' to translate a request into a command.\n");
    printf("Pipelines: cmd1 | cmd2 | cmd3\n");
}

typedef struct { int json; int count; } help_json_ctx_t;

static void help_json_each(const cmd_spec_t *s, void *ud)
{
    help_json_ctx_t *ctx = ud;
    if (ctx->count > 0) printf(",\n");
    printf("  {\"name\":");
    json_print_string(stdout, s->name);
    printf(",\"summary\":");
    json_print_string(stdout, s->summary ? s->summary : "");
    printf(",\"long_help\":");
    json_print_string(stdout, s->long_help ? s->long_help : "");
    printf("}");
    ctx->count++;
}

static int run_help(int argc, char **argv)
{
    int json = 0;
    const char *subcmd = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
        else if (argv[i][0] != '-')         subcmd = argv[i];
    }

    if (subcmd) {
        const cmd_spec_t *s = find_command(subcmd);
        if (!s) {
            fprintf(stderr, "help: unknown command: %s\n", subcmd);
            return 1;
        }
        if (json) {
            printf("{\"name\":");
            json_print_string(stdout, s->name);
            printf(",\"summary\":");
            json_print_string(stdout, s->summary ? s->summary : "");
            printf(",\"long_help\":");
            json_print_string(stdout, s->long_help ? s->long_help : "");
            printf("}\n");
        } else {
            if (s->print_usage) s->print_usage(stdout);
        }
        return 0;
    }

    if (json) {
        printf("{\"shell\":\"%s\",\"version\":\"%s\",\"commands\":[\n",
               SHELL_NAME, SHELL_VERSION);
        help_json_ctx_t ctx = {1, 0};
        for_each_command(help_json_each, &ctx);
        printf("\n]}\n");
    } else {
        print_help_text();
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatch single command (no pipeline)                                */
/* ------------------------------------------------------------------ */

static int dispatch_command_argv(int argc, char **argv)
{
    if (argc == 0) return 0;

    const char *name = argv[0];

    /* builtins */
    if (strcmp(name, "exit") == 0 || strcmp(name, "quit") == 0) {
        history_save();
        exit(0);
    }
    if (strcmp(name, "help") == 0) {
        return run_help(argc, argv);
    }
    if (strcmp(name, "version") == 0 ||
        (strcmp(name, "--version") == 0)) {
        printf("%s %s\n", SHELL_NAME, SHELL_VERSION);
        return 0;
    }

    /* registered commands */
    const cmd_spec_t *cmd = find_command(name);
    if (cmd) {
        return cmd->run(argc, argv);
    }

    fprintf(stderr, "%s: %s: command not found\n", SHELL_NAME, name);
    return 127;
}

/* ------------------------------------------------------------------ */
/* Pipeline dispatcher                                                  */
/* ------------------------------------------------------------------ */

/*
 * Split argv on "|" tokens, wire pipes between segments,
 * fork+exec each using execvp or in-process dispatch.
 */
static int dispatch_pipeline(int argc, char **argv)
{
    /* find pipe positions */
    int pipe_pos[MAX_ARGS];
    int npipes = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            pipe_pos[npipes++] = i;
        }
    }

    if (npipes == 0) {
        return dispatch_command_argv(argc, argv);
    }

    int nseg = npipes + 1;
    /* segment starts */
    int seg_start[MAX_ARGS];
    int seg_end[MAX_ARGS];   /* exclusive */
    seg_start[0] = 0;
    for (int i = 0; i < npipes; i++) {
        seg_end[i]     = pipe_pos[i];
        seg_start[i+1] = pipe_pos[i] + 1;
    }
    seg_end[npipes] = argc;

    /* create pipe fds */
    int pipefds[MAX_ARGS][2];
    for (int i = 0; i < npipes; i++) {
        if (pipe(pipefds[i]) != 0) {
            perror("pipe");
            return 1;
        }
    }

    pid_t pids[MAX_ARGS];

    for (int seg = 0; seg < nseg; seg++) {
        int s_argc = seg_end[seg] - seg_start[seg];
        char **s_argv = argv + seg_start[seg];

        if (s_argc == 0) {
            fprintf(stderr, "%s: empty pipeline segment\n", SHELL_NAME);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }

        if (pid == 0) {
            /* child: wire stdin from previous pipe */
            if (seg > 0) {
                dup2(pipefds[seg-1][0], STDIN_FILENO);
            }
            /* wire stdout to next pipe */
            if (seg < nseg - 1) {
                dup2(pipefds[seg][1], STDOUT_FILENO);
            }
            /* close all pipe fds */
            for (int i = 0; i < npipes; i++) {
                close(pipefds[i][0]);
                close(pipefds[i][1]);
            }

            /* try execvp first, fall back to in-process */
            s_argv[s_argc] = NULL;

            /* check registered command */
            const cmd_spec_t *cmd = find_command(s_argv[0]);
            if (cmd) {
                int ret = cmd->run(s_argc, s_argv);
                fflush(stdout);
                _exit(ret);
            }

            /* builtins that make sense in pipeline */
            if (strcmp(s_argv[0], "help") == 0) {
                int ret = run_help(s_argc, s_argv);
                fflush(stdout);
                _exit(ret);
            }

            /* try external */
            execvp(s_argv[0], s_argv);
            fprintf(stderr, "%s: %s: command not found\n", SHELL_NAME, s_argv[0]);
            fflush(stderr);
            _exit(127);
        }

        pids[seg] = pid;
    }

    /* close all pipe fds in parent */
    for (int i = 0; i < npipes; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    int status = 0;
    for (int seg = 0; seg < nseg; seg++) {
        int ws;
        waitpid(pids[seg], &ws, 0);
        if (WIFEXITED(ws) && WEXITSTATUS(ws) != 0)
            status = WEXITSTATUS(ws);
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* dispatch_pipeline_from_argv: used when shell called with | in args   */
/* ------------------------------------------------------------------ */

static int dispatch_pipeline_from_argv(int argc, char **argv)
{
    /* argv[0] is the shell binary, argv[1..] is the command */
    return dispatch_pipeline(argc - 1, argv + 1);
}

/* ------------------------------------------------------------------ */
/* dispatch_command: called when argc >= 2 without pipeline             */
/* ------------------------------------------------------------------ */

static int dispatch_command(int argc, char **argv)
{
    /* argv[0] = shell binary, argv[1] = command name */
    return dispatch_command_argv(argc - 1, argv + 1);
}

/* ------------------------------------------------------------------ */
/* Terminal raw mode                                                     */
/* ------------------------------------------------------------------ */

static struct termios orig_termios;

static void restore_terminal(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void set_raw_mode(void)
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);

    struct termios raw = orig_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

/* ------------------------------------------------------------------ */
/* Tab completion                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *prefix;
    char  matches[64][256];
    int   count;
} completion_ctx_t;

static void collect_cmd(const cmd_spec_t *s, void *ud)
{
    completion_ctx_t *ctx = ud;
    if (ctx->count >= 64) return;
    if (strncmp(s->name, ctx->prefix, strlen(ctx->prefix)) == 0) {
        strncpy(ctx->matches[ctx->count++], s->name, 255);
    }
}

static int complete_word(const char *buf, int cursor,
                         char *word_out, size_t word_outsz,
                         char completions[][256], int *ncomp)
{
    /* find start of current word */
    int ws = cursor;
    while (ws > 0 && buf[ws-1] != ' ' && buf[ws-1] != '\t') ws--;

    size_t wlen = (size_t)(cursor - ws);
    if (wlen >= word_outsz) wlen = word_outsz - 1;
    memcpy(word_out, buf + ws, wlen);
    word_out[wlen] = '\0';

    *ncomp = 0;

    /* is this the first word? */
    int first_word = 1;
    for (int i = 0; i < ws; i++) {
        if (buf[i] != ' ' && buf[i] != '\t') { first_word = 0; break; }
    }

    if (first_word) {
        /* complete command names */
        completion_ctx_t ctx;
        ctx.prefix = word_out;
        ctx.count  = 0;
        for_each_command(collect_cmd, &ctx);

        /* also add builtins */
        const char *builtins[] = { "help", "exit", "quit", "version", NULL };
        for (int i = 0; builtins[i]; i++) {
            if (strncmp(builtins[i], word_out, wlen) == 0 && ctx.count < 64) {
                strncpy(ctx.matches[ctx.count++], builtins[i], 255);
            }
        }

        *ncomp = ctx.count;
        for (int i = 0; i < ctx.count && i < 64; i++) {
            strncpy(completions[i], ctx.matches[i], 255);
        }
    } else {
        /* complete file paths */
        const char *slash = strrchr(word_out, '/');
        const char *fprefix;
        char dirpath[1024] = ".";

        if (slash) {
            fprefix = slash + 1;
            size_t dlen = (size_t)(slash - word_out);
            if (dlen == 0) {
                strncpy(dirpath, "/", sizeof(dirpath)-1);
            } else if (dlen < sizeof(dirpath)) {
                memcpy(dirpath, word_out, dlen);
                dirpath[dlen] = '\0';
            }
        } else {
            fprefix = word_out;
        }

        DIR *d = opendir(dirpath);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL && *ncomp < 64) {
                if (ent->d_name[0] == '.') continue;
                if (strncmp(ent->d_name, fprefix, strlen(fprefix)) == 0) {
                    if (slash) {
                        int dlen = (int)(slash - word_out);
                        if (dlen > 250) dlen = 250;
                        memcpy(completions[*ncomp], word_out, (size_t)dlen);
                        completions[*ncomp][dlen] = '/';
                        strncpy(completions[*ncomp] + dlen + 1,
                                ent->d_name, 255 - (size_t)dlen - 1);
                        completions[*ncomp][255] = '\0';
                    } else {
                        strncpy(completions[*ncomp], ent->d_name, 255);
                    }
                    (*ncomp)++;
                }
            }
            closedir(d);
        }
    }

    return ws; /* word start position */
}

/* ------------------------------------------------------------------ */
/* Line editor                                                           */
/* ------------------------------------------------------------------ */

static char *read_line_raw(const char *prompt)
{
    static char buf[MAX_LINE];
    int len = 0;
    int cursor = 0;
    int hist_idx = history_count; /* pointing past end = current line */
    char saved_line[MAX_LINE] = "";

    fputs(prompt, stdout);
    fflush(stdout);

    while (1) {
        unsigned char c;
        ssize_t nr = read(STDIN_FILENO, &c, 1);
        if (nr <= 0) {
            if (nr == 0) {
                /* EOF / Ctrl-D on empty line */
                if (len == 0) {
                    printf("\n");
                    history_save();
                    exit(0);
                }
                /* Ctrl-D with text: delete char */
                if (cursor < len) {
                    memmove(buf + cursor, buf + cursor + 1,
                            (size_t)(len - cursor - 1));
                    len--;
                    buf[len] = '\0';
                    /* redraw */
                    printf("\r\033[K%s%s\033[%dG",
                           prompt, buf, (int)(strlen(prompt) + (size_t)cursor + 1));
                    fflush(stdout);
                }
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            printf("\n");
            fflush(stdout);
            return buf;
        }

        if (c == 4) {
            /* Ctrl-D */
            if (len == 0) {
                printf("\n");
                history_save();
                exit(0);
            }
            continue;
        }

        if (c == 3) {
            /* Ctrl-C */
            printf("^C\n");
            buf[0] = '\0';
            return buf;
        }

        if (c == 12) {
            /* Ctrl-L: clear screen */
            printf("\033[H\033[J");
            printf("%s%s", prompt, buf);
            printf("\033[%dG", (int)(strlen(prompt) + (size_t)cursor + 1));
            fflush(stdout);
            continue;
        }

        if (c == 127 || c == 8) {
            /* Backspace */
            if (cursor > 0) {
                memmove(buf + cursor - 1, buf + cursor,
                        (size_t)(len - cursor));
                cursor--;
                len--;
                buf[len] = '\0';
                printf("\r\033[K%s%s", prompt, buf);
                if (cursor < len)
                    printf("\033[%dG", (int)(strlen(prompt) + (size_t)cursor + 1));
                fflush(stdout);
            }
            continue;
        }

        if (c == 9) {
            /* Tab: completion */
            char word[256];
            char comps[64][256];
            int ncomp = 0;
            int wstart = complete_word(buf, cursor, word, sizeof(word),
                                       comps, &ncomp);

            if (ncomp == 1) {
                /* single match: insert */
                const char *ins = comps[0] + strlen(word);
                size_t ins_len = strlen(ins);
                if (len + (int)ins_len < MAX_LINE - 1) {
                    memmove(buf + cursor + ins_len, buf + cursor,
                            (size_t)(len - cursor));
                    memcpy(buf + cursor, ins, ins_len);
                    cursor += (int)ins_len;
                    len    += (int)ins_len;
                    buf[len] = '\0';
                    /* add trailing space for commands */
                    if (cursor == len && len + 1 < MAX_LINE - 1) {
                        buf[len++] = ' ';
                        buf[len] = '\0';
                        cursor++;
                    }
                }
            } else if (ncomp > 1) {
                /* show matches */
                printf("\n");
                for (int i = 0; i < ncomp; i++)
                    printf("  %s\n", comps[i]);

                /* find common prefix */
                int cplen = (int)strlen(comps[0]);
                for (int i = 1; i < ncomp; i++) {
                    int j = 0;
                    while (j < cplen &&
                           comps[0][j] == comps[i][j]) j++;
                    cplen = j;
                }
                const char *cp_ins = comps[0] + strlen(word);
                int cp_ins_len = cplen - (int)strlen(word);
                if (cp_ins_len > 0 &&
                    len + cp_ins_len < MAX_LINE - 1) {
                    memmove(buf + cursor + cp_ins_len, buf + cursor,
                            (size_t)(len - cursor));
                    memcpy(buf + cursor, cp_ins, (size_t)cp_ins_len);
                    cursor += cp_ins_len;
                    len    += cp_ins_len;
                    buf[len] = '\0';
                }
            }

            (void)wstart;
            printf("\r\033[K%s%s", prompt, buf);
            if (cursor < len)
                printf("\033[%dG", (int)(strlen(prompt) + (size_t)cursor + 1));
            fflush(stdout);
            continue;
        }

        if (c == 27) {
            /* Escape sequence */
            unsigned char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
                switch (seq[1]) {
                case 'A': /* Up */
                    if (hist_idx > 0) {
                        if (hist_idx == history_count)
                            strncpy(saved_line, buf, MAX_LINE - 1);
                        hist_idx--;
                        strncpy(buf, history[hist_idx], MAX_LINE - 1);
                        len = cursor = (int)strlen(buf);
                        printf("\r\033[K%s%s", prompt, buf);
                        fflush(stdout);
                    }
                    break;
                case 'B': /* Down */
                    if (hist_idx < history_count) {
                        hist_idx++;
                        if (hist_idx == history_count) {
                            strncpy(buf, saved_line, MAX_LINE - 1);
                        } else {
                            strncpy(buf, history[hist_idx], MAX_LINE - 1);
                        }
                        len = cursor = (int)strlen(buf);
                        printf("\r\033[K%s%s", prompt, buf);
                        fflush(stdout);
                    }
                    break;
                case 'C': /* Right */
                    if (cursor < len) {
                        cursor++;
                        printf("\033[C");
                        fflush(stdout);
                    }
                    break;
                case 'D': /* Left */
                    if (cursor > 0) {
                        cursor--;
                        printf("\033[D");
                        fflush(stdout);
                    }
                    break;
                case 'H': /* Home */
                    cursor = 0;
                    printf("\033[%dG", (int)strlen(prompt) + 1);
                    fflush(stdout);
                    break;
                case 'F': /* End */
                    cursor = len;
                    printf("\033[%dG", (int)(strlen(prompt) + (size_t)len + 1));
                    fflush(stdout);
                    break;
                case '3': /* Delete key: ESC[3~ */
                    read(STDIN_FILENO, &seq[2], 1);
                    if (cursor < len) {
                        memmove(buf + cursor, buf + cursor + 1,
                                (size_t)(len - cursor - 1));
                        len--;
                        buf[len] = '\0';
                        printf("\r\033[K%s%s", prompt, buf);
                        if (cursor < len)
                            printf("\033[%dG",
                                   (int)(strlen(prompt) + (size_t)cursor + 1));
                        fflush(stdout);
                    }
                    break;
                }
            }
            continue;
        }

        /* printable char: insert at cursor */
        if (c >= 32 && len < MAX_LINE - 1) {
            memmove(buf + cursor + 1, buf + cursor,
                    (size_t)(len - cursor));
            buf[cursor++] = (char)c;
            len++;
            buf[len] = '\0';
            printf("\r\033[K%s%s", prompt, buf);
            if (cursor < len)
                printf("\033[%dG", (int)(strlen(prompt) + (size_t)cursor + 1));
            fflush(stdout);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Natural-language @ handler                                            */
/* ------------------------------------------------------------------ */

static int handle_at_command(const char *line, int interactive)
{
    /* skip the '@' */
    const char *query = line + 1;
    while (*query == ' ') query++;

    char suggestion[MAX_LINE];
    if (!fallback_nl_to_command(query, suggestion, sizeof(suggestion))) {
        fprintf(stderr, "%s: @ could not translate: %s\n", SHELL_NAME, query);
        return 1;
    }

    if (!command_is_safe(suggestion)) {
        fprintf(stderr, "%s: @ suggestion rejected (unsafe): %s\n",
                SHELL_NAME, suggestion);
        return 1;
    }

    printf("AI suggestion: %s\n", suggestion);

    if (!interactive) {
        printf("Not running suggestion in non-interactive mode.\n");
        return 0;
    }

    printf("Run it? [y/N] ");
    fflush(stdout);

    /* temporarily restore canonical mode for the y/n prompt */
    restore_terminal();
    char answer[16];
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        set_raw_mode();
        printf("\n");
        return 0;
    }
    set_raw_mode();

    if (answer[0] == 'y' || answer[0] == 'Y') {
        char cmd_copy[MAX_LINE];
        strncpy(cmd_copy, suggestion, MAX_LINE - 1);
        cmd_copy[MAX_LINE - 1] = '\0';
        char *argv[MAX_ARGS];
        int   argc = tokenize(cmd_copy, argv, MAX_ARGS);
        return dispatch_pipeline(argc, argv);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Interactive shell                                                     */
/* ------------------------------------------------------------------ */

static void run_interactive_shell(void)
{
    int interactive = isatty(STDIN_FILENO);

    if (interactive) {
        printf("%s %s  (type 'help' for commands, 'exit' to quit)\n",
               SHELL_NAME, SHELL_VERSION);
        set_raw_mode();
    }

    while (1) {
        char *line;

        if (interactive) {
            line = read_line_raw(SHELL_NAME "> ");
        } else {
            static char buf[MAX_LINE];
            if (fgets(buf, sizeof(buf), stdin) == NULL) break;
            buf[strcspn(buf, "\n")] = '\0';
            line = buf;
        }

        if (!line || line[0] == '\0') continue;

        /* trim leading/trailing whitespace */
        while (*line == ' ' || *line == '\t') line++;
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == ' ' || line[l-1] == '\t')) line[--l] = '\0';
        if (l == 0) continue;

        history_add(line);

        /* @ natural language interface */
        if (line[0] == '@') {
            handle_at_command(line, interactive);
            continue;
        }

        /* tokenize and dispatch */
        char line_copy[MAX_LINE];
        strncpy(line_copy, line, MAX_LINE - 1);
        line_copy[MAX_LINE - 1] = '\0';

        char *argv[MAX_ARGS];
        int   argc = tokenize(line_copy, argv, MAX_ARGS);
        if (argc == 0) continue;

        dispatch_pipeline(argc, argv);
    }

    if (interactive) {
        printf("\n");
    }
    history_save();
}

/* ------------------------------------------------------------------ */
/* main                                                                  */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    register_all_builtin_commands();
    history_load();

    if (argc < 2) {
        run_interactive_shell();
        return 0;
    }

    /* check for pipeline in args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            return dispatch_pipeline_from_argv(argc, argv);
        }
    }

    return dispatch_command(argc, argv);
}
