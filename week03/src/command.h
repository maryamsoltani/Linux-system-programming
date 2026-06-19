/*
 * command.h — parsed command structure and parser API.
 *
 * The parser turns a token stream into an array of Command values.
 * Each Command represents one program invocation:
 *
 *     ls -l > out.txt | grep .c &
 *
 * becomes two commands:
 *     [0] argv = {"ls", "-l", NULL}, stdout_file = "out.txt", sep = "|"
 *     [1] argv = {"grep", ".c", NULL},                        sep = "&"
 *
 * The separator after each command tells the executor what to do next:
 *
 *   ";"  — wait for this command to finish, then continue
 *   "&"  — don't wait; run in background, reap later via SIGCHLD
 *   "|"  — pipe stdout to next command's stdin
 *
 * The parser also expands wildcards (glob) inside argv, so
 *     ls *.c
 * produces argv = {"ls", "main.c", "util.c", "token.c", NULL} (or whatever
 * matches in the cwd).
 */

#ifndef COMMAND_H
#define COMMAND_H

#define MAX_COMMANDS 256

/* Command separators — defined as string constants so the executor can
 * use strcmp() rather than character comparisons.  The tokenizer hands
 * us pointers to the same kind of single-char strings. */
#define SEP_PIPE   "|"
#define SEP_BG     "&"
#define SEP_SEQ    ";"

typedef struct Command {
    char  **argv;          /* NULL-terminated, owned (must be freed)   */
    char   *stdin_file;    /* points into the original line buffer     */
    char   *stdout_file;   /* points into the original line buffer     */
    int     append_stdout; /* 1 if >> was used, 0 for >                */
    char   *sep;           /* always one of SEP_PIPE / SEP_BG / SEP_SEQ */
} Command;

/* Initialise a Command to safe defaults (everything NULL/zero). */
void command_init(Command *cmd);

/* Free argv strings and the argv array.  Safe to call multiple times. */
void command_free(Command *cmd);

/* Parse a token array into commands.
 *
 * Returns the number of commands on success, or:
 *   -1 — generic syntax error (consecutive separators, leading separator)
 *   -2 — pipe at end of line (`ls |`)
 *   -3 — redirection target missing (`echo hi >`)
 *
 * On error, any commands already filled in are freed before return.
 */
int parse_commands(char **tokens, Command *commands);

#endif /* COMMAND_H */
