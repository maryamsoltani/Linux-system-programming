#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "shell.h"

/* ── built-in names ───────────────────────────────────────────────────────── */

static const char *BUILTINS[] = {
    "cd", "pwd", "exit", "help", NULL
};

int is_builtin(const char *name)
{
    for (int i = 0; BUILTINS[i]; i++)
        if (strcmp(name, BUILTINS[i]) == 0)
            return 1;
    return 0;
}

/* ── individual built-ins ─────────────────────────────────────────────────── */

static int builtin_cd(Command *cmd)
{
    const char *dir = cmd->argc > 1 ? cmd->argv[1] : getenv("HOME");
    if (!dir) {
        fprintf(stderr, "cd: HOME not set\n");
        return 1;
    }
    if (chdir(dir) != 0) {
        perror("cd");
        return 1;
    }
    return 0;
}

static int builtin_pwd(void)
{
    char buf[512];
    if (!getcwd(buf, sizeof(buf))) {
        perror("pwd");
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

static int builtin_help(void)
{
    printf("aishell — week05 shell\n");
    printf("Built-in commands:\n");
    printf("  cd  [dir]   change directory\n");
    printf("  pwd         print working directory\n");
    printf("  exit        quit the shell\n");
    printf("  help        show this message\n");
    printf("Any other command is run via fork/exec.\n");
    return 0;
}

/* ── dispatcher ───────────────────────────────────────────────────────────── */

int run_builtin(Command *cmd)
{
    if (strcmp(cmd->argv[0], "cd")   == 0) return builtin_cd(cmd);
    if (strcmp(cmd->argv[0], "pwd")  == 0) return builtin_pwd();
    if (strcmp(cmd->argv[0], "help") == 0) return builtin_help();
    if (strcmp(cmd->argv[0], "exit") == 0) exit(0);
    return 1;
}
