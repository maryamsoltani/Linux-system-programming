/*
 * cmd_jobs.c — "jobs" command module.
 *
 * Anatomy:
 *   jobs_run()         — lists all background and stopped jobs
 *   jobs_print_usage() — prints help using the same argtable definitions
 *   cmd_jobs_spec      — the cmd_spec_t descriptor registered at startup
 *
 * The job list is defined externally in psh.c and accessed via the
 * extern declaration below.
 */

#include <stdio.h>
#include <stdlib.h>
#include <argtable2.h>
#include "cmd_spec.h"

/* Forward declaration */
extern const cmd_spec_t cmd_jobs_spec;

/* Forward declaration — job list lives in psh.c */
extern void listjobs_external(FILE *out);

/* -----------------------------------------------------------------------
 * Shared argtable builder
 * ----------------------------------------------------------------------- */
static void build_jobs_argtable(
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
 * jobs_print_usage
 * ----------------------------------------------------------------------- */
void jobs_print_usage(FILE *out)
{
    struct arg_lit *help;
    struct arg_end *end;
    void          **argtable;

    build_jobs_argtable(&help, &end, &argtable);

    fprintf(out, "\nUsage: jobs ");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n", cmd_jobs_spec.long_help);
    fprintf(out, "\nOptions:\n");
    arg_print_glossary(out, argtable, "  %-20s %s\n");
    fprintf(out, "\n");
}

/* -----------------------------------------------------------------------
 * jobs_run
 * ----------------------------------------------------------------------- */
int jobs_run(int argc, char **argv)
{
    struct arg_lit *help;
    struct arg_end *end;
    void          **argtable;
    int             nerrors;

    build_jobs_argtable(&help, &end, &argtable);
    nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        jobs_print_usage(stdout);
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stdout, end, "jobs");
        jobs_print_usage(stdout);
        return 1;
    }

    /* Delegate to the shell's job list printer */
    listjobs_external(stdout);
    return 0;
}

/* -----------------------------------------------------------------------
 * cmd_jobs_spec
 * ----------------------------------------------------------------------- */
const cmd_spec_t cmd_jobs_spec = {
    .name        = "jobs",
    .summary     = "list background and stopped jobs",
    .long_help   = "Display all currently active background and stopped jobs,\n"
                   "showing each job's ID (JID), process ID (PID), state, and command.",
    .run         = jobs_run,
    .print_usage = jobs_print_usage,
};
