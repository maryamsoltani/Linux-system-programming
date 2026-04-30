/*
 * cmd_help.c — "help" command module.
 *
 * With no arguments: lists all registered commands (registry_list).
 * With a command name: calls that command's print_usage() function.
 *
 * This is the glue between the registry and the app anatomy — it proves
 * that documentation is auto-derived from the cmd_spec_t descriptors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argtable2.h>
#include "cmd_spec.h"

/* Forward declaration */
extern const cmd_spec_t cmd_help_spec;

/* -----------------------------------------------------------------------
 * Shared argtable builder
 * ----------------------------------------------------------------------- */
static void build_help_argtable(
    struct arg_lit  **help,
    struct arg_str  **topic,
    struct arg_end  **end,
    void           ***argtable_out)
{
    static void *argtable[4];

    *help  = arg_lit0("h", "help",  "show this help message and exit");
    *topic = arg_str0(NULL, NULL, "[command]",
                      "command to show detailed help for");
    *end   = arg_end(10);

    argtable[0] = *help;
    argtable[1] = *topic;
    argtable[2] = *end;
    argtable[3] = NULL;

    *argtable_out = argtable;
}

/* -----------------------------------------------------------------------
 * help_print_usage
 * ----------------------------------------------------------------------- */
void help_print_usage(FILE *out)
{
    struct arg_lit *help;
    struct arg_str *topic;
    struct arg_end *end;
    void          **argtable;

    build_help_argtable(&help, &topic, &end, &argtable);

    fprintf(out, "\nUsage: help ");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n", cmd_help_spec.long_help);
    fprintf(out, "\nOptions:\n");
    arg_print_glossary(out, argtable, "  %-20s %s\n");
    fprintf(out, "\n");
}

/* -----------------------------------------------------------------------
 * help_run
 * ----------------------------------------------------------------------- */
int help_run(int argc, char **argv)
{
    struct arg_lit *help;
    struct arg_str *topic;
    struct arg_end *end;
    void          **argtable;
    int             nerrors;

    build_help_argtable(&help, &topic, &end, &argtable);
    nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        help_print_usage(stdout);
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stdout, end, "help");
        help_print_usage(stdout);
        return 1;
    }

    if (topic->count > 0) {
        /* Show detailed help for a specific command */
        const cmd_spec_t *spec = registry_find(topic->sval[0]);
        if (spec == NULL) {
            fprintf(stdout, "help: unknown command '%s'\n", topic->sval[0]);
            return 1;
        }
        if (spec->print_usage != NULL)
            spec->print_usage(stdout);
        else
            fprintf(stdout, "%s: %s\n", spec->name, spec->summary);
    } else {
        /* No argument — list all commands */
        registry_list(stdout);
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * cmd_help_spec
 * ----------------------------------------------------------------------- */
const cmd_spec_t cmd_help_spec = {
    .name        = "help",
    .summary     = "show help for built-in commands",
    .long_help   = "With no arguments, lists all registered built-in commands.\n"
                   "With a command name, shows detailed usage for that command.",
    .run         = help_run,
    .print_usage = help_print_usage,
};
