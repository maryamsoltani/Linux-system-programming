#include <stdio.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_clear.h"

int clear_run(int argc, char **argv)
{
    int json = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            clear_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "clear: invalid option: %s\n", argv[i]);
            clear_print_usage(stderr);
            return 1;
        }
    }

    if (json) {
        printf("{\"command\":\"clear\",\"cleared\":true}\n");
    } else {
        printf("\033[H\033[J");
        fflush(stdout);
    }

    return 0;
}

void clear_print_usage(FILE *out)
{
    fprintf(out, "Usage: clear [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Clear the terminal screen.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_clear_spec = {
    .name        = "clear",
    .summary     = "clear the terminal screen",
    .long_help   = "Clear the terminal screen.",
    .run         = clear_run,
    .print_usage = clear_print_usage,
};

void register_clear_command(void)
{
    register_command(&cmd_clear_spec);
}
