#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cmd_spec.h"
#include "cmd_localdate.h"

int localdate_run(int argc, char **argv)
{
    int json = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            localdate_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "localdate: invalid option: %s\n", argv[i]);
            localdate_print_usage(stderr);
            return 1;
        }
    }

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm == NULL) {
        fprintf(stderr, "localdate: failed to get local time\n");
        return 1;
    }

    if (json) {
        printf("{\"date\":\"%04d-%02d-%02d\",\"year\":%d,\"month\":%d,\"day\":%d}\n",
               tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
               tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    } else {
        printf("Local Date: %04d-%02d-%02d\n",
               tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    }

    return 0;
}

void localdate_print_usage(FILE *out)
{
    fprintf(out, "Usage: localdate [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print the current local date.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_localdate_spec = {
    .name        = "localdate",
    .summary     = "print the current local date",
    .long_help   = "Print the current local date in YYYY-MM-DD format.",
    .run         = localdate_run,
    .print_usage = localdate_print_usage,
};

void register_localdate_command(void)
{
    register_command(&cmd_localdate_spec);
}
