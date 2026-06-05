#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_pwd.h"
#include "json_utils.h"

int pwd_run(int argc, char **argv)
{
    int json = 0;
    int logical = 1;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            pwd_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-L") == 0) {
            logical = 1;
        } else if (strcmp(argv[i], "-P") == 0) {
            logical = 0;
        } else {
            fprintf(stderr, "pwd: invalid option: %s\n", argv[i]);
            pwd_print_usage(stderr);
            return 1;
        }
    }

    char *cwd = NULL;
    if (logical) {
        cwd = getenv("PWD");
    }
    char buf[4096];
    if (cwd == NULL) {
        if (getcwd(buf, sizeof(buf)) == NULL) {
            perror("pwd");
            return 1;
        }
        cwd = buf;
        logical = 0;
    }

    if (json) {
        printf("{\"cwd\":");
        json_print_string(stdout, cwd);
        printf(",\"logical\":%s}\n", logical ? "true" : "false");
    } else {
        printf("%s\n", cwd);
    }

    return 0;
}

void pwd_print_usage(FILE *out)
{
    fprintf(out, "Usage: pwd [-L] [-P] [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print the current working directory.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-L", "use logical PWD (default)");
    fprintf(out, "  %-20s %s\n", "-P", "use physical path (getcwd)");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_pwd_spec = {
    .name        = "pwd",
    .summary     = "print working directory",
    .long_help   = "Print the current working directory.",
    .run         = pwd_run,
    .print_usage = pwd_print_usage,
};

void register_pwd_command(void)
{
    register_command(&cmd_pwd_spec);
}
