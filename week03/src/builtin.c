/*
 * builtin.c — built-in shell commands.
 *
 * Each built-in is a function with signature `int fn(Command *cmd)`.
 * They are dispatched through a static table at the bottom of this file,
 * which is what `builtin_is()` and `builtin_run()` walk.
 *
 * `prompt` mutates the global prompt string defined in mish.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "builtin.h"

/* Defined in mish.c — the prompt the REPL prints each line. */
extern char *g_prompt;

/* ------------------------------------------------------------------------
 * prompt — change the prompt text.
 *   prompt           → restore default ("% ")
 *   prompt foo$      → prompt becomes "foo$ "
 * --------------------------------------------------------------------- */
static int do_prompt(Command *cmd)
{
    const char *new_prompt = (cmd->argv[1] != NULL) ? cmd->argv[1] : "%";
    char       *buf;
    size_t      n = strlen(new_prompt);

    /* Always append a trailing space so users don't have to type it. */
    buf = malloc(n + 2);
    if (buf == NULL) {
        perror("prompt");
        return -1;
    }
    memcpy(buf, new_prompt, n);
    buf[n]     = ' ';
    buf[n + 1] = '\0';

    free(g_prompt);
    g_prompt = buf;
    return 0;
}

/* ------------------------------------------------------------------------
 * pwd — print working directory.
 * --------------------------------------------------------------------- */
static int do_pwd(Command *cmd)
{
    char buf[4096];
    (void)cmd;
    if (getcwd(buf, sizeof(buf)) == NULL) {
        perror("pwd");
        return -1;
    }
    printf("%s\n", buf);
    return 0;
}

/* ------------------------------------------------------------------------
 * cd — change working directory.
 *   cd        → $HOME
 *   cd <dir>  → that directory
 * --------------------------------------------------------------------- */
static int do_cd(Command *cmd)
{
    const char *target;
    if (cmd->argv[1] == NULL) {
        target = getenv("HOME");
        if (target == NULL) {
            fprintf(stderr, "cd: HOME not set\n");
            return -1;
        }
    } else {
        target = cmd->argv[1];
    }
    if (chdir(target) != 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * exit — return 1 to signal "stop the REPL".
 * --------------------------------------------------------------------- */
static int do_exit(Command *cmd)
{
    (void)cmd;
    return 1;
}

/* ------------------------------------------------------------------------
 * help — list built-ins.
 * --------------------------------------------------------------------- */
static int do_help(Command *cmd);

/* ------------------------------------------------------------------------
 * Dispatch table
 * --------------------------------------------------------------------- */
typedef int (*builtin_fn)(Command *cmd);

static const struct {
    const char *name;
    builtin_fn  fn;
    const char *summary;
} BUILTINS[] = {
    { "prompt", do_prompt, "set the prompt text"          },
    { "pwd",    do_pwd,    "print the working directory"   },
    { "cd",     do_cd,     "change directory"              },
    { "exit",   do_exit,   "exit the shell"                },
    { "help",   do_help,   "list built-in commands"        },
    { NULL,     NULL,      NULL                             }
};

static int do_help(Command *cmd)
{
    int i;
    (void)cmd;
    printf("Built-in commands:\n");
    for (i = 0; BUILTINS[i].name != NULL; i++)
        printf("  %-8s  %s\n", BUILTINS[i].name, BUILTINS[i].summary);
    return 0;
}

int builtin_is(const char *name)
{
    int i;
    if (name == NULL) return 0;
    for (i = 0; BUILTINS[i].name != NULL; i++)
        if (strcmp(name, BUILTINS[i].name) == 0)
            return 1;
    return 0;
}

int builtin_run(Command *cmd)
{
    int i;
    if (cmd->argv == NULL || cmd->argv[0] == NULL)
        return -1;
    for (i = 0; BUILTINS[i].name != NULL; i++)
        if (strcmp(cmd->argv[0], BUILTINS[i].name) == 0)
            return BUILTINS[i].fn(cmd);
    return -1;  /* shouldn't get here if caller checked builtin_is() */
}
