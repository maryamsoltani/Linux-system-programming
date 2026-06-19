#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"

/* ── parser ───────────────────────────────────────────────────────────────── */

/* Fill one Command by tokenizing a single segment (no '|' inside). */
static void parse_segment(char *seg, Command *cmd)
{
    cmd->argc       = 0;
    cmd->background = 0;

    char *token = strtok(seg, " \t\n");
    while (token && cmd->argc < MAX_ARGS - 1) {
        cmd->argv[cmd->argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    cmd->argv[cmd->argc] = NULL;

    /* trailing '&' marks background */
    if (cmd->argc > 0 && strcmp(cmd->argv[cmd->argc - 1], "&") == 0) {
        cmd->background = 1;
        cmd->argv[--cmd->argc] = NULL;
    }
}

/*
 * parse_pipeline() — split the raw line on '|', fill pl->cmds[].
 *
 * We cannot use strtok() for splitting on '|' and then again inside
 * parse_segment(), because strtok() is not reentrant — the second call
 * would reset the shared internal pointer.  Instead we manually scan for
 * '|', null-terminate each segment, and collect pointers BEFORE calling
 * parse_segment().
 */
static void parse_pipeline(char *line, Pipeline *pl)
{
    pl->num_cmds   = 0;
    pl->background = 0;

    /* collect segment start pointers by replacing '|' with '\0' */
    char *segs[MAX_CMDS];
    int   n = 0;
    segs[n++] = line;
    for (char *p = line; *p && n < MAX_CMDS; p++) {
        if (*p == '|') {
            *p = '\0';
            segs[n++] = p + 1;
        }
    }

    /* now parse each segment independently */
    for (int i = 0; i < n; i++) {
        parse_segment(segs[i], &pl->cmds[pl->num_cmds]);
        if (pl->cmds[pl->num_cmds].argc > 0)
            pl->num_cmds++;
    }

    /* background flag lives on the last command */
    if (pl->num_cmds > 0)
        pl->background = pl->cmds[pl->num_cmds - 1].background;
}

/* ── REPL ─────────────────────────────────────────────────────────────────── */

void shell_loop(void)
{
    char line[MAX_LINE];

    while (1) {
        printf("aishell> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        if (line[0] == '\n' || line[0] == '\0')
            continue;

        Pipeline pl;
        parse_pipeline(line, &pl);
        if (pl.num_cmds == 0)
            continue;

        /* single built-in command — run directly, no fork */
        if (pl.num_cmds == 1 && is_builtin(pl.cmds[0].argv[0])) {
            run_builtin(&pl.cmds[0]);
            continue;
        }

        run_pipeline(&pl);
    }
}

int main(void)
{
    setup_signals();
    shell_loop();
    return 0;
}
