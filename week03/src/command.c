/*
 * command.c — token-stream parser + redirection + wildcard expansion.
 *
 * Three passes per call to parse_commands():
 *   1. split tokens into spans separated by | & ; — fill .sep on each
 *   2. for each span, extract redirection targets (< / > / >>)
 *   3. for each span, build argv with glob expansion on remaining tokens
 *
 * argv is heap-allocated (each string strdup'd) so it survives until the
 * executor finishes with the command and calls command_free().
 *
 * The parser also handles `>>` as append-redirection, which is one of
 * the small additions over the original Week 3 spec.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include "command.h"
#include "token.h"

/* ------------------------------------------------------------------------
 * Construction / destruction
 * --------------------------------------------------------------------- */
void command_init(Command *cmd)
{
    cmd->argv          = NULL;
    cmd->stdin_file    = NULL;
    cmd->stdout_file   = NULL;
    cmd->append_stdout = 0;
    cmd->sep           = NULL;
}

void command_free(Command *cmd)
{
    int i;
    if (cmd->argv == NULL)
        return;
    for (i = 0; cmd->argv[i] != NULL; i++)
        free(cmd->argv[i]);
    free(cmd->argv);
    cmd->argv = NULL;
}

/* ------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */
static int is_separator(const char *t)
{
    return strcmp(t, SEP_PIPE) == 0
        || strcmp(t, SEP_BG)   == 0
        || strcmp(t, SEP_SEQ)  == 0;
}

static int is_redirect(const char *t)
{
    return strcmp(t, "<")  == 0
        || strcmp(t, ">")  == 0
        || strcmp(t, ">>") == 0;
}

/* ------------------------------------------------------------------------
 * Pass 2: extract redirection targets within a span [first..last] and
 * mark them so the argv builder knows to skip them.
 *
 * `keep` is parallel to tokens and is set to 0 for tokens that should
 * be excluded from argv (the redirect operators and their targets).
 * --------------------------------------------------------------------- */
static int extract_redirections(char **tokens, int first, int last,
                                int *keep, Command *cmd)
{
    int i;
    for (i = first; i <= last; i++) {
        if (!is_redirect(tokens[i]))
            continue;

        /* Need an operand */
        if (i + 1 > last)
            return -3;

        if (strcmp(tokens[i], "<") == 0) {
            cmd->stdin_file = tokens[i + 1];
        } else if (strcmp(tokens[i], ">>") == 0) {
            cmd->stdout_file   = tokens[i + 1];
            cmd->append_stdout = 1;
        } else { /* > */
            cmd->stdout_file   = tokens[i + 1];
            cmd->append_stdout = 0;
        }
        keep[i]     = 0;
        keep[i + 1] = 0;
        i++;  /* skip the operand */
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Pass 3: build argv from kept tokens, expanding globs.
 *
 * GLOB_NOCHECK means a pattern with no matches is returned as-is, which
 * is the behaviour we want — `ls nonexistent*` should still try to ls
 * the literal string and let ls produce the error.
 * --------------------------------------------------------------------- */
static int build_argv(char **tokens, int first, int last,
                      int *keep, Command *cmd)
{
    glob_t  g;
    size_t  capacity = 8;
    size_t  count    = 0;
    char  **argv     = malloc(capacity * sizeof(*argv));
    int     i;
    size_t  k;

    if (argv == NULL)
        return -1;

    for (i = first; i <= last; i++) {
        if (!keep[i])
            continue;

        if (glob(tokens[i], GLOB_NOCHECK, NULL, &g) != 0) {
            /* Out of memory or other glob failure — fall back to literal */
            if (count + 1 >= capacity) {
                capacity *= 2;
                char **bigger = realloc(argv, capacity * sizeof(*argv));
                if (bigger == NULL) goto oom;
                argv = bigger;
            }
            argv[count] = strdup(tokens[i]);
            if (argv[count] == NULL) goto oom;
            count++;
            continue;
        }

        for (k = 0; k < g.gl_pathc; k++) {
            if (count + 1 >= capacity) {
                capacity *= 2;
                char **bigger = realloc(argv, capacity * sizeof(*argv));
                if (bigger == NULL) { globfree(&g); goto oom; }
                argv = bigger;
            }
            argv[count] = strdup(g.gl_pathv[k]);
            if (argv[count] == NULL) { globfree(&g); goto oom; }
            count++;
        }
        globfree(&g);
    }
    argv[count] = NULL;
    cmd->argv   = argv;
    return 0;

oom:
    for (k = 0; k < count; k++)
        free(argv[k]);
    free(argv);
    cmd->argv = NULL;
    return -1;
}

/* ------------------------------------------------------------------------
 * parse_commands
 * --------------------------------------------------------------------- */
int parse_commands(char **tokens, Command *commands)
{
    int n_tokens = 0;
    int i, c = 0;
    int span_start = 0;
    int keep[MAX_TOKENS];

    while (tokens[n_tokens] != NULL)
        n_tokens++;

    if (n_tokens == 0)
        return 0;

    /* Reject a leading separator */
    if (is_separator(tokens[0]))
        return -1;

    /* If the line doesn't end in a separator, append a synthetic ";"
     * so the loop below treats the trailing command uniformly. */
    char *appended_sep = NULL;
    if (!is_separator(tokens[n_tokens - 1])) {
        appended_sep = SEP_SEQ;
        tokens[n_tokens] = SEP_SEQ;
        n_tokens++;
    }

    for (i = 0; i < MAX_TOKENS; i++)
        keep[i] = 1;

    for (i = 0; i < n_tokens; i++) {
        if (!is_separator(tokens[i]))
            continue;

        /* Two separators in a row → empty command */
        if (i == span_start) {
            for (int j = 0; j < c; j++)
                command_free(&commands[j]);
            if (appended_sep) tokens[n_tokens - 1] = NULL;
            return -1;
        }

        if (c >= MAX_COMMANDS) {
            for (int j = 0; j < c; j++)
                command_free(&commands[j]);
            if (appended_sep) tokens[n_tokens - 1] = NULL;
            return -1;
        }

        command_init(&commands[c]);
        commands[c].sep = tokens[i];

        int rc = extract_redirections(tokens, span_start, i - 1, keep, &commands[c]);
        if (rc < 0) {
            for (int j = 0; j <= c; j++)
                command_free(&commands[j]);
            if (appended_sep) tokens[n_tokens - 1] = NULL;
            return rc;
        }

        rc = build_argv(tokens, span_start, i - 1, keep, &commands[c]);
        if (rc < 0) {
            for (int j = 0; j <= c; j++)
                command_free(&commands[j]);
            if (appended_sep) tokens[n_tokens - 1] = NULL;
            return -1;
        }

        c++;
        span_start = i + 1;
    }

    /* Reject `cmd |` — pipe with no command after it */
    if (c > 0 && strcmp(commands[c - 1].sep, SEP_PIPE) == 0
              && span_start >= n_tokens) {
        for (int j = 0; j < c; j++)
            command_free(&commands[j]);
        if (appended_sep) tokens[n_tokens - 1] = NULL;
        return -2;
    }

    if (appended_sep) tokens[n_tokens - 1] = NULL;
    return c;
}
