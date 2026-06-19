/*
 * builtin.h — built-in commands.
 *
 * Built-ins run inside the shell process.  They cannot be put on the
 * left side of a pipe (they don't fork) — the executor handles this
 * by giving them inherited stdio.  cd, prompt, and exit are also
 * meaningless when forked because their effects (cwd change, prompt
 * change, shell termination) wouldn't survive the child exit.
 *
 * Each builtin returns:
 *    0 — success, keep running
 *    1 — success, request shell exit (used by `exit`)
 *   -1 — error
 */

#ifndef BUILTIN_H
#define BUILTIN_H

#include "command.h"

/* Returns 1 if argv[0] is a built-in command name, 0 otherwise. */
int builtin_is(const char *name);

/* Run the built-in identified by cmd->argv[0].  Returns the value
 * from the matching builtin handler (see top of file). */
int builtin_run(Command *cmd);

#endif /* BUILTIN_H */
