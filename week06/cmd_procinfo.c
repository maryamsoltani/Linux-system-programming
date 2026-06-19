#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_procinfo.h"

int procinfo_run(int argc, char **argv)
{
    int json = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            procinfo_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "procinfo: invalid option: %s\n", argv[i]);
            procinfo_print_usage(stderr);
            return 1;
        }
    }

    pid_t pid  = getpid();
    pid_t ppid = getppid();

    if (json) {
        printf("{\"pid\":%d,\"ppid\":%d}\n", (int)pid, (int)ppid);
    } else {
        printf("pid=%d ppid=%d\n", (int)pid, (int)ppid);
    }

    return 0;
}

void procinfo_print_usage(FILE *out)
{
    fprintf(out, "Usage: procinfo [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print the current process ID and parent process ID.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_procinfo_spec = {
    .name        = "procinfo",
    .summary     = "print process ID and parent process ID",
    .long_help   = "Print pid and ppid of the current process.",
    .run         = procinfo_run,
    .print_usage = procinfo_print_usage,
};

void register_procinfo_command(void)
{
    register_command(&cmd_procinfo_spec);
}
