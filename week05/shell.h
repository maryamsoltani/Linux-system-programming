#ifndef SHELL_H
#define SHELL_H

#define MAX_ARGS    64      /* max tokens per command line */
#define MAX_LINE   256      /* max characters per input line */
#define MAX_CMDS    16      /* max commands in a pipeline */

/*
 * Parsed command: argv[0] is the program name, argv[argc] == NULL.
 * background == 1 means the user appended '&'.
 */
typedef struct {
    char *argv[MAX_ARGS];
    int   argc;
    int   background;   /* 1 if command ends with '&' */
} Command;

/*
 * Pipeline: one or more commands separated by '|'.
 * e.g. "ls -l | grep .c | wc -l"  →  cmds[0..2], num_cmds = 3
 */
typedef struct {
    Command cmds[MAX_CMDS];
    int     num_cmds;
    int     background;   /* inherited from last command's '&' */
} Pipeline;

/* builtins.c */
int  is_builtin(const char *name);
int  run_builtin(Command *cmd);

/* execute.c */
int  launch(Command *cmd);          /* single command: fork + exec */
int  run_pipeline(Pipeline *pl);    /* one or more commands chained with pipes */

/* signals.c */
void setup_signals(void);

/* shell.c */
void shell_loop(void);

#endif /* SHELL_H */
