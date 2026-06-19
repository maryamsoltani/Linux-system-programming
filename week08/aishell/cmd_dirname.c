#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_dirname.h"
#include "json_utils.h"

int dirname_run(int argc, char **argv)
{
    int json = 0;
    int i;
    int arg_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            dirname_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "dirname: invalid option: %s\n", argv[i]);
            dirname_print_usage(stderr);
            return 1;
        } else {
            arg_start = i;
            break;
        }
    }

    if (arg_start >= argc) {
        fprintf(stderr, "dirname: missing operand\n");
        dirname_print_usage(stderr);
        return 1;
    }

    if (json) printf("{\"paths\":[\n");
    int first = 1;

    for (i = arg_start; i < argc; i++) {
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s", argv[i]);
        char *dir = dirname(tmp);

        if (json) {
            if (!first) printf(",\n");
            printf("  {\"path\":");
            json_print_string(stdout, argv[i]);
            printf(",\"dirname\":");
            json_print_string(stdout, dir);
            printf("}");
            first = 0;
        } else {
            printf("%s\n", dir);
        }
    }

    if (json) printf("\n]}\n");

    return 0;
}

void dirname_print_usage(FILE *out)
{
    fprintf(out, "Usage: dirname [--json] PATH...\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print the directory portion of each PATH.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_dirname_spec = {
    .name        = "dirname",
    .summary     = "print directory portion of path",
    .long_help   = "Print the directory component of each file path.",
    .run         = dirname_run,
    .print_usage = dirname_print_usage,
};

void register_dirname_command(void)
{
    register_command(&cmd_dirname_spec);
}
