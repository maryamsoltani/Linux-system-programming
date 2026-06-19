#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cmd_spec.h"
#include "cmd_du.h"
#include "json_utils.h"

int du_run(int argc, char **argv)
{
    int json = 0;
    int i;
    int arg_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            du_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "du: invalid option: %s\n", argv[i]);
            du_print_usage(stderr);
            return 1;
        } else {
            arg_start = i;
            break;
        }
    }

    if (arg_start >= argc) {
        /* default: current directory */
        static const char *dot[] = { "." };
        argv = (char **)dot;
        arg_start = 0;
        argc = 1;
    }

    if (json) printf("{\"paths\":[\n");
    int first = 1;
    int status = 0;

    for (i = arg_start; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) != 0) {
            fprintf(stderr, "du: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        /* st_blocks is in 512-byte units; convert to KB */
        long kb = (long)(st.st_blocks / 2);

        if (json) {
            if (!first) printf(",\n");
            printf("  {\"path\":");
            json_print_string(stdout, argv[i]);
            printf(",\"kilobytes\":%ld}", kb);
            first = 0;
        } else {
            printf("%ld\t%s\n", kb, argv[i]);
        }
    }

    if (json) printf("\n]}\n");

    return status;
}

void du_print_usage(FILE *out)
{
    fprintf(out, "Usage: du [--json] [PATH...]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Show disk usage in kilobytes.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_du_spec = {
    .name        = "du",
    .summary     = "show disk usage",
    .long_help   = "Show disk usage in kilobytes for each path.",
    .run         = du_run,
    .print_usage = du_print_usage,
};

void register_du_command(void)
{
    register_command(&cmd_du_spec);
}
