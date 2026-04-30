/*
 * cmd_quit.c — "quit" command module.
 *
 * Anatomy:
 *   quit_run()         — parses args via argtable3, exits the shell
 *   quit_print_usage() — prints help using the same argtable definitions
 *   cmd_quit_spec      — the cmd_spec_t descriptor registered at startup
 */

#include <stdio.h>
#include <stdlib.h>
#include <argtable2.h>
#include "cmd_spec.h"

/* Forward declaration */
extern const cmd_spec_t cmd_quit_spec;

/* -----------------------------------------------------------------------
 * Shared argtable builder — single source of truth for options.
 * Both run() and print_usage() call this to get the same definitions.
 * ----------------------------------------------------------------------- */
static void build_quit_argtable(
    struct arg_lit **help,
    struct arg_end **end,
    void          ***argtable_out)
{
    static void *argtable[3];

    *help  = arg_lit0("h", "help", "show this help message and exit");
    *end   = arg_end(10);

    argtable[0] = *help;
    argtable[1] = *end;
    argtable[2] = NULL;

    *argtable_out = argtable;
}

/* -----------------------------------------------------------------------
 * quit_print_usage
 * ----------------------------------------------------------------------- */
void quit_print_usage(FILE *out)
{
    struct arg_lit *help;
    struct arg_end *end;
    void          **argtable;

    build_quit_argtable(&help, &end, &argtable);

    fprintf(out, "\nUsage: quit ");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n", cmd_quit_spec.long_help);
    fprintf(out, "\nOptions:\n");
    arg_print_glossary(out, argtable, "  %-20s %s\n");
    fprintf(out, "\n");
}

/* -----------------------------------------------------------------------
 * quit_run
 * ----------------------------------------------------------------------- */
int quit_run(int argc, char **argv)
{
    struct arg_lit *help;
    struct arg_end *end;
    void          **argtable;
    int             nerrors;

    build_quit_argtable(&help, &end, &argtable);
    nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        quit_print_usage(stdout);
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stdout, end, "quit");
        quit_print_usage(stdout);
        return 1;
    }

    /* Exit the shell */
    exit(0);
    return 0; /* unreachable */
}

/* -----------------------------------------------------------------------
 * cmd_quit_spec — the anatomy descriptor registered with the shell
 * ----------------------------------------------------------------------- */
const cmd_spec_t cmd_quit_spec = {
    .name       = "quit",
    .summary    = "exit the shell",
    .long_help  = "Terminate the psh shell session immediately.",
    .run        = quit_run,
    .print_usage = quit_print_usage,
};
