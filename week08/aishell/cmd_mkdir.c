#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cmd_spec.h"
#include "cmd_mkdir.h"
#include "json_utils.h"

static int mkdir_parents(const char *path, mode_t mode)
{
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
    return 0;
}

int mkdir_run(int argc, char **argv)
{
    int parents = 0;
    int verbose = 0;
    int dry_run = 0;
    int json = 0;
    mode_t mode = 0755;
    int i;
    int dir_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            mkdir_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--parents") == 0) {
            parents = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = 1;
        } else if (strcmp(argv[i], "-m") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "mkdir: -m requires an argument\n");
                return 1;
            }
            mode = (mode_t)strtol(argv[++i], NULL, 8);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "mkdir: invalid option: %s\n", argv[i]);
            mkdir_print_usage(stderr);
            return 1;
        } else {
            dir_start = i;
            break;
        }
    }

    if (dir_start >= argc) {
        fprintf(stderr, "mkdir: missing directory operand\n");
        mkdir_print_usage(stderr);
        return 1;
    }

    int status = 0;
    char mode_str[8];
    snprintf(mode_str, sizeof(mode_str), "%03o", (unsigned)mode);

    if (json) printf("{\"command\":\"mkdir\",\"parents\":%s,\"dry_run\":%s,\"mode\":\"%s\",\"directories\":[\n",
                     parents ? "true" : "false",
                     dry_run  ? "true" : "false",
                     mode_str);

    int first = 1;
    for (i = dir_start; i < argc; i++) {
        int ok = 1;
        if (!dry_run) {
            int ret;
            if (parents) {
                ret = mkdir_parents(argv[i], mode);
            } else {
                ret = mkdir(argv[i], mode);
            }
            if (ret != 0) {
                fprintf(stderr, "mkdir: %s: %s\n", argv[i], strerror(errno));
                status = 1;
                ok = 0;
            }
        }
        if (verbose && !json && ok) printf("mkdir: created directory '%s'\n", argv[i]);
        if (json) {
            if (!first) printf(",\n");
            printf("  {\"path\":");
            json_print_string(stdout, argv[i]);
            printf(",\"created\":%s}", ok ? "true" : "false");
            first = 0;
        }
    }

    if (json) printf("\n],\"success\":%s}\n", status == 0 ? "true" : "false");

    return status;
}

void mkdir_print_usage(FILE *out)
{
    fprintf(out, "Usage: mkdir [-p] [-v] [--dry-run] [-m MODE] [--json] DIR...\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Create directories.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-p, --parents", "create parent directories as needed");
    fprintf(out, "  %-20s %s\n", "-v, --verbose", "print each created directory");
    fprintf(out, "  %-20s %s\n", "--dry-run", "show what would be done");
    fprintf(out, "  %-20s %s\n", "-m MODE", "set directory permissions (octal)");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_mkdir_spec = {
    .name        = "mkdir",
    .summary     = "create directories",
    .long_help   = "Create one or more directories.",
    .run         = mkdir_run,
    .print_usage = mkdir_print_usage,
};

void register_mkdir_command(void)
{
    register_command(&cmd_mkdir_spec);
}
