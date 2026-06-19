#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_mv.h"
#include "json_utils.h"

int mv_run(int argc, char **argv)
{
    int json = 0;
    int i;
    int arg_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            mv_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "mv: invalid option: %s\n", argv[i]);
            mv_print_usage(stderr);
            return 1;
        } else {
            arg_start = i;
            break;
        }
    }

    int remaining = argc - arg_start;
    if (remaining < 2) {
        fprintf(stderr, "mv: missing source or destination\n");
        mv_print_usage(stderr);
        return 1;
    }

    const char *src  = argv[arg_start];
    const char *dest = argv[arg_start + 1];

    if (rename(src, dest) != 0) {
        fprintf(stderr, "mv: cannot move '%s' to '%s': %s\n",
                src, dest, strerror(errno));
        return 1;
    }

    if (json) {
        printf("{\"command\":\"mv\",\"source\":");
        json_print_string(stdout, src);
        printf(",\"destination\":");
        json_print_string(stdout, dest);
        printf(",\"moved\":true}\n");
    }

    return 0;
}

void mv_print_usage(FILE *out)
{
    fprintf(out, "Usage: mv [--json] SOURCE DEST\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Move (rename) SOURCE to DEST.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_mv_spec = {
    .name        = "mv",
    .summary     = "move or rename files",
    .long_help   = "Move (rename) SOURCE to DEST.",
    .run         = mv_run,
    .print_usage = mv_print_usage,
};

void register_mv_command(void)
{
    register_command(&cmd_mv_spec);
}
