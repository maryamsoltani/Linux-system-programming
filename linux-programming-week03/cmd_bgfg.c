/*
 * cmd_bgfg.c — "bg" and "fg" command modules.
 *
 * Both commands share the same argtable builder because they accept
 * exactly the same arguments: an optional JID (%n) or PID.
 *
 * Anatomy (two specs in one file):
 *   bgfg_run(cmd, argc, argv) — shared implementation
 *   bg_run() / fg_run()       — thin wrappers that call bgfg_run
 *   bg_print_usage()          — prints bg-specific help
 *   fg_print_usage()          — prints fg-specific help
 *   cmd_bg_spec / cmd_fg_spec — the two descriptors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <argtable2.h>
#include "cmd_spec.h"

/* Forward declarations */
extern const cmd_spec_t cmd_bg_spec;
extern const cmd_spec_t cmd_fg_spec;

/* Forward declarations — job control lives in psh.c */
extern void do_bgfg_external(char **argv);

/* -----------------------------------------------------------------------
 * Shared argtable builder for both bg and fg
 * ----------------------------------------------------------------------- */
static void build_bgfg_argtable(
    struct arg_lit  **help,
    struct arg_str  **job,
    struct arg_end  **end,
    void           ***argtable_out)
{
    static void *argtable[4];

    *help = arg_lit0("h", "help",  "show this help message and exit");
    *job  = arg_str1(NULL, NULL, "%job|pid",
                     "job to resume: JID as %%1, %%2 ... or numeric PID");
    *end  = arg_end(10);

    argtable[0] = *help;
    argtable[1] = *job;
    argtable[2] = *end;
    argtable[3] = NULL;

    *argtable_out = argtable;
}

/* -----------------------------------------------------------------------
 * bg_print_usage
 * ----------------------------------------------------------------------- */
void bg_print_usage(FILE *out)
{
    struct arg_lit *help;
    struct arg_str *job;
    struct arg_end *end;
    void          **argtable;

    build_bgfg_argtable(&help, &job, &end, &argtable);

    fprintf(out, "\nUsage: bg ");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n", cmd_bg_spec.long_help);
    fprintf(out, "\nOptions:\n");
    arg_print_glossary(out, argtable, "  %-20s %s\n");
    fprintf(out, "\nExamples:\n");
    fprintf(out, "  bg %%1        resume stopped job 1 in the background\n");
    fprintf(out, "  bg 4521      resume process 4521 in the background\n\n");
}

/* -----------------------------------------------------------------------
 * fg_print_usage
 * ----------------------------------------------------------------------- */
void fg_print_usage(FILE *out)
{
    struct arg_lit *help;
    struct arg_str *job;
    struct arg_end *end;
    void          **argtable;

    build_bgfg_argtable(&help, &job, &end, &argtable);

    fprintf(out, "\nUsage: fg ");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n", cmd_fg_spec.long_help);
    fprintf(out, "\nOptions:\n");
    arg_print_glossary(out, argtable, "  %-20s %s\n");
    fprintf(out, "\nExamples:\n");
    fprintf(out, "  fg %%1        bring job 1 to the foreground\n");
    fprintf(out, "  fg 4521      bring process 4521 to the foreground\n\n");
}

/* -----------------------------------------------------------------------
 * Shared implementation for both bg and fg
 * ----------------------------------------------------------------------- */
static int bgfg_run(const char *cmd, int argc, char **argv)
{
    struct arg_lit *help;
    struct arg_str *job;
    struct arg_end *end;
    void          **argtable;
    int             nerrors;

    build_bgfg_argtable(&help, &job, &end, &argtable);
    nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        if (strcmp(cmd, "bg") == 0) bg_print_usage(stdout);
        else                        fg_print_usage(stdout);
        return 0;
    }
    if (nerrors > 0) {
        arg_print_errors(stdout, end, cmd);
        if (strcmp(cmd, "bg") == 0) bg_print_usage(stdout);
        else                        fg_print_usage(stdout);
        return 1;
    }

    /*
     * Re-build argv in the form that do_bgfg_external expects:
     *   argv[0] = "bg" or "fg"
     *   argv[1] = the job argument ("%1" or "4521")
     *   argv[2] = NULL
     */
    char *new_argv[3];
    new_argv[0] = (char *)cmd;
    new_argv[1] = (char *)job->sval[0];
    new_argv[2] = NULL;

    do_bgfg_external(new_argv);
    return 0;
}

/* -----------------------------------------------------------------------
 * bg_run / fg_run — thin wrappers
 * ----------------------------------------------------------------------- */
int bg_run(int argc, char **argv) { return bgfg_run("bg", argc, argv); }
int fg_run(int argc, char **argv) { return bgfg_run("fg", argc, argv); }

/* -----------------------------------------------------------------------
 * cmd_bg_spec / cmd_fg_spec
 * ----------------------------------------------------------------------- */
const cmd_spec_t cmd_bg_spec = {
    .name        = "bg",
    .summary     = "resume a stopped job in the background",
    .long_help   = "Send SIGCONT to a stopped job and continue it in the background.\n"
                   "The job stays in the job list and can be brought to the foreground\n"
                   "with the 'fg' command.",
    .run         = bg_run,
    .print_usage = bg_print_usage,
};

const cmd_spec_t cmd_fg_spec = {
    .name        = "fg",
    .summary     = "bring a job to the foreground",
    .long_help   = "Send SIGCONT to a stopped or background job and run it in the\n"
                   "foreground.  The shell waits for it to finish or stop before\n"
                   "returning to the prompt.",
    .run         = fg_run,
    .print_usage = fg_print_usage,
};
