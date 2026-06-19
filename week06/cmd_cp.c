#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_cp.h"
#include "json_utils.h"

int cp_run(int argc, char **argv)
{
    int json = 0;
    int i;
    int arg_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            cp_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "cp: invalid option: %s\n", argv[i]);
            cp_print_usage(stderr);
            return 1;
        } else {
            arg_start = i;
            break;
        }
    }

    int remaining = argc - arg_start;
    if (remaining < 2) {
        fprintf(stderr, "cp: missing source or destination\n");
        cp_print_usage(stderr);
        return 1;
    }

    const char *src  = argv[arg_start];
    const char *dest = argv[arg_start + 1];

    FILE *fsrc = fopen(src, "rb");
    if (!fsrc) {
        fprintf(stderr, "cp: %s: %s\n", src, strerror(errno));
        return 1;
    }
    FILE *fdest = fopen(dest, "wb");
    if (!fdest) {
        fprintf(stderr, "cp: %s: %s\n", dest, strerror(errno));
        fclose(fsrc);
        return 1;
    }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        if (fwrite(buf, 1, n, fdest) != n) {
            fprintf(stderr, "cp: write error: %s\n", strerror(errno));
            fclose(fsrc); fclose(fdest);
            return 1;
        }
    }
    fclose(fsrc);
    fclose(fdest);

    if (json) {
        printf("{\"command\":\"cp\",\"source\":");
        json_print_string(stdout, src);
        printf(",\"destination\":");
        json_print_string(stdout, dest);
        printf(",\"copied\":true}\n");
    }

    return 0;
}

void cp_print_usage(FILE *out)
{
    fprintf(out, "Usage: cp [--json] SOURCE DEST\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Copy SOURCE to DEST.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_cp_spec = {
    .name        = "cp",
    .summary     = "copy files",
    .long_help   = "Copy SOURCE to DEST.",
    .run         = cp_run,
    .print_usage = cp_print_usage,
};

void register_cp_command(void)
{
    register_command(&cmd_cp_spec);
}
