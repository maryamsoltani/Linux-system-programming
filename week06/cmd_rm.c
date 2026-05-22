#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_rm.h"
#include "json_utils.h"

int rm_run(int argc, char **argv)
{
    int json = 0;
    int force = 0;
    int i;
    int file_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            rm_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "rm: invalid option: %s\n", argv[i]);
            rm_print_usage(stderr);
            return 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (file_start >= argc) {
        fprintf(stderr, "rm: missing file operand\n");
        rm_print_usage(stderr);
        return 1;
    }

    int status = 0;
    if (json) printf("{\"command\":\"rm\",\"removed\":[\n");
    int first = 1;

    for (i = file_start; i < argc; i++) {
        int ok = 1;
        if (unlink(argv[i]) != 0) {
            if (!force || errno != ENOENT) {
                fprintf(stderr, "rm: %s: %s\n", argv[i], strerror(errno));
                status = 1;
                ok = 0;
            }
        }
        if (json) {
            if (!first) printf(",\n");
            printf("  {\"path\":");
            json_print_string(stdout, argv[i]);
            printf(",\"success\":%s}", ok ? "true" : "false");
            first = 0;
        }
    }

    if (json) printf("\n],\"success\":%s}\n", status == 0 ? "true" : "false");

    return status;
}

void rm_print_usage(FILE *out)
{
    fprintf(out, "Usage: rm [-f] [--json] FILE...\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Remove files.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-f, --force", "ignore missing files");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_rm_spec = {
    .name        = "rm",
    .summary     = "remove files",
    .long_help   = "Remove one or more files.",
    .run         = rm_run,
    .print_usage = rm_print_usage,
};

void register_rm_command(void)
{
    register_command(&cmd_rm_spec);
}
