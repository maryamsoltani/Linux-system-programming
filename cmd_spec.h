/*
 * cmd_spec.h — Command Anatomy: standard interface for every command module.
 *
 * Every command in this shell ecosystem (built-in or standalone) must define
 * exactly one cmd_spec_t that describes itself.  The shell's registry uses
 * these specs to dispatch commands, print help, and (in future) build packages.
 *
 * Anatomy required by each command module:
 *   1.  cmd_spec_t  <name>_spec          — the spec struct (metadata + fn ptrs)
 *   2.  int         <name>_run(argc, argv) — parses args with argtable3, runs logic
 *   3.  void        <name>_print_usage(FILE*) — prints help via same argtable3 defs
 */

#ifndef CMD_SPEC_H
#define CMD_SPEC_H

#include <stdio.h>

/* -----------------------------------------------------------------------
 * cmd_spec_t
 * The single standard descriptor for any command in this shell ecosystem.
 * ----------------------------------------------------------------------- */
typedef struct cmd_spec {
    const char *name;       /* command name shown at the prompt, e.g. "jobs"  */
    const char *summary;    /* one-line description used in help listings      */
    const char *long_help;  /* longer Markdown-style description (may be NULL) */

    /* Main entry point.  Receives argc/argv just like a real main().
     * Must use argtable3 (or argtable2) for all argument parsing.
     * Returns 0 on success, non-zero on error.                               */
    int  (*run)(int argc, char **argv);

    /* Print usage + option glossary to `out`.
     * Must use the SAME argtable definitions as run() — one shared builder.  */
    void (*print_usage)(FILE *out);
} cmd_spec_t;


/* -----------------------------------------------------------------------
 * Registry API
 * A simple in-memory array of cmd_spec_t pointers that the shell queries
 * on every command dispatch.
 * ----------------------------------------------------------------------- */
#define REGISTRY_MAX 64

/* Register one command spec.  Returns 0 on success, -1 if registry is full. */
int  registry_register(const cmd_spec_t *spec);

/* Find a registered command by name.  Returns NULL if not found.            */
const cmd_spec_t *registry_find(const char *name);

/* Print a table of all registered commands (name + summary) to `out`.      */
void registry_list(FILE *out);

#endif /* CMD_SPEC_H */
