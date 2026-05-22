#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <utime.h>

#include "cmd_spec.h"
#include "cmd_touch.h"
#include "json_utils.h"

int touch_run(int argc, char **argv)
{
    int no_create = 0;
    int verbose = 0;
    int access_only = 0;
    int modify_only = 0;
    int json = 0;
    const char *time_str = NULL;
    int i;
    int file_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            touch_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--no-create") == 0) {
            no_create = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            access_only = 1;
        } else if (strcmp(argv[i], "-m") == 0) {
            modify_only = 1;
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "touch: -t requires an argument\n");
                return 1;
            }
            time_str = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "touch: invalid option: %s\n", argv[i]);
            touch_print_usage(stderr);
            return 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (file_start >= argc) {
        fprintf(stderr, "touch: missing file operand\n");
        touch_print_usage(stderr);
        return 1;
    }

    /* Parse time if given: YYYYMMDDhhmm */
    time_t set_time = 0;
    if (time_str != NULL) {
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        if (sscanf(time_str, "%4d%2d%2d%2d%2d",
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min) != 5) {
            fprintf(stderr, "touch: invalid time format: %s\n", time_str);
            return 1;
        }
        tm.tm_year -= 1900;
        tm.tm_mon  -= 1;
        set_time = mktime(&tm);
        if (set_time == (time_t)-1) {
            fprintf(stderr, "touch: invalid time: %s\n", time_str);
            return 1;
        }
    }

    int status = 0;
    if (json) printf("{\"command\":\"touch\",\"files\":[\n");
    int first = 1;

    for (i = file_start; i < argc; i++) {
        struct stat st;
        int exists = (stat(argv[i], &st) == 0);

        if (!exists) {
            if (no_create) {
                /* skip */
                if (json) {
                    if (!first) printf(",\n");
                    printf("  {\"path\":");
                    json_print_string(stdout, argv[i]);
                    printf(",\"action\":\"skipped\"}");
                    first = 0;
                }
                continue;
            }
            /* create file */
            int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
            if (fd < 0) {
                fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
                status = 1;
                continue;
            }
            close(fd);
            if (verbose && !json) printf("touch: created '%s'\n", argv[i]);
        }

        /* Update timestamps */
        if (time_str != NULL || access_only || modify_only) {
            struct utimbuf ut;
            struct stat st2;
            stat(argv[i], &st2);
            ut.actime  = st2.st_atime;
            ut.modtime = st2.st_mtime;
            time_t t = time_str ? set_time : time(NULL);
            if (!modify_only) ut.actime  = t;
            if (!access_only)  ut.modtime = t;
            if (utime(argv[i], &ut) != 0) {
                fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
                status = 1;
            }
        } else {
            if (utime(argv[i], NULL) != 0) {
                fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno));
                status = 1;
            }
        }

        if (verbose && !json) printf("touch: updated '%s'\n", argv[i]);

        if (json) {
            if (!first) printf(",\n");
            printf("  {\"path\":");
            json_print_string(stdout, argv[i]);
            printf(",\"action\":\"%s\"}", exists ? "updated" : "created");
            first = 0;
        }
    }

    if (json) printf("\n],\"success\":%s}\n", status == 0 ? "true" : "false");

    return status;
}

void touch_print_usage(FILE *out)
{
    fprintf(out, "Usage: touch [-c] [-v] [-a] [-m] [-t TIME] [--json] FILE...\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Create or update file timestamps.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-c, --no-create", "do not create missing files");
    fprintf(out, "  %-20s %s\n", "-v, --verbose", "print touched file names");
    fprintf(out, "  %-20s %s\n", "-a", "change access time only");
    fprintf(out, "  %-20s %s\n", "-m", "change modification time only");
    fprintf(out, "  %-20s %s\n", "-t TIME", "use [[CC]YY]MMDDhhmm[.ss] time");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_touch_spec = {
    .name        = "touch",
    .summary     = "change file timestamps",
    .long_help   = "Create or update timestamps on files.",
    .run         = touch_run,
    .print_usage = touch_print_usage,
};

void register_touch_command(void)
{
    register_command(&cmd_touch_spec);
}
