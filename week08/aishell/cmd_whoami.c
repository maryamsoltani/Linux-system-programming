#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_whoami.h"
#include "json_utils.h"

int whoami_run(int argc, char **argv)
{
    int json = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            whoami_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "whoami: invalid option: %s\n", argv[i]);
            whoami_print_usage(stderr);
            return 1;
        }
    }

    struct passwd *pw = getpwuid(getuid());
    const char *name = (pw != NULL) ? pw->pw_name : "unknown";

    if (json) {
        printf("{\"username\":");
        json_print_string(stdout, name);
        printf("}\n");
    } else {
        printf("%s\n", name);
    }

    return 0;
}

void whoami_print_usage(FILE *out)
{
    fprintf(out, "Usage: whoami [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print the current user name.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_whoami_spec = {
    .name        = "whoami",
    .summary     = "print current user name",
    .long_help   = "Print the name of the current user.",
    .run         = whoami_run,
    .print_usage = whoami_print_usage,
};

void register_whoami_command(void)
{
    register_command(&cmd_whoami_spec);
}
