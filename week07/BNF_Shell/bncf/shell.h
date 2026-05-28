#ifndef SHELL_H
#define SHELL_H

/* Maximum number of arguments per command. */
#define MAX_ARGS 128

/*
 * Command: a single executable unit with its arguments,
 * optional I/O redirections, and a background flag.
 *
 * For a pipeline  cmd1 | cmd2 | cmd3, the caller builds
 * an array of Commands and wires them together with pipes.
 */
typedef struct Command {
    char  *name;            /* argv[0], the command name          */
    int    argc;            /* total number of arguments          */
    char  *argv[MAX_ARGS];  /* NULL-terminated argument vector    */
    char  *infile;          /* NULL or path for stdin redirect    */
    char  *outfile;         /* NULL or path for stdout redirect   */
    int    background;      /* 1 if job runs in background        */
} Command;

/* Execute a single (non-pipelined) command. */
void execute_command(const Command *cmd);

/* Execute a pipeline of n commands. */
void execute_pipeline(Command *cmds, int n, int background);

/* Register built-in command modules (called once at startup). */
void register_all_builtin_commands(void);

#endif
