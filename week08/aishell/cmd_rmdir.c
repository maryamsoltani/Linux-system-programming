#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_rmdir.h"
#include "json_utils.h"

int rmdir_run(int argc, char **argv)
{
    int json = 0;
    int i;
    int dir_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            rmdir_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "rmdir: invalid option: %s\n", argv[i]);
            rmdir_print_usage(stderr);
            return 1;
        } else {
            dir_start = i;
            break;
        }
    }

    if (dir_start >= argc) {
        fprintf(stderr, "rmdir: missing directory operand\n");
        rmdir_print_usage(stderr);
        return 1;
    }

    int status = 0;
    if (json) printf("{\"command\":\"rmdir\",\"directories\":[\n");
    int first = 1;

    for (i = dir_start; i < argc; i++) {
        int ok = 1;
        if (rmdir(argv[i]) != 0) {
            fprintf(stderr, "rmdir: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            ok = 0;
        }
        if (json) {
            if (!first) printf(",\n");
            printf("  {\"path\":");
            json_print_string(stdout, argv[i]);
            printf(",\"removed\":%s}", ok ? "true" : "false");
            first = 0;
        }
    }

    if (json) printf("\n],\"success\":%s}\n", status == 0 ? "true" : "false");

    return status;
}

void rmdir_print_usage(FILE *out)
{
    fprintf(out, "Usage: rmdir [--json] DIR...\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Remove empty directories.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_rmdir_spec = {
    .name        = "rmdir",
    .summary     = "remove empty directories",
    .long_help   = "Remove one or more empty directories.",
    .run         = rmdir_run,
    .print_usage = rmdir_print_usage,
};

void register_rmdir_command(void)
{
    register_command(&cmd_rmdir_spec);
}
