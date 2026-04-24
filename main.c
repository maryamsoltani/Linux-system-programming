#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/cmd_spec.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * AiShell — Main Dispatcher
 *
 * Two invocation modes:
 *   1. Combined binary:   ./aishell <command> [args...]
 *   2. Symlinked binary:  ./ls [args...]   (argv[0] == command name)
 *
 * The dispatcher looks up the command in COMMAND_REGISTRY (cmd_spec_t[])
 * and calls spec->run(argc, argv). This replaces the old CmdEntry table.
 * ───────────────────────────────────────────────────────────────────────── */

static void print_registry(void) {
    printf("AiShell — Command Registry\n");
    printf("==========================\n");
    printf("%-22s  %-12s  %s\n", "COMMAND", "CATEGORY", "SUMMARY");
    printf("%-22s  %-12s  %s\n", "-------", "--------", "-------");

    const char *last_cat = "";
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        if (strcmp((*s)->category, last_cat) != 0) {
            printf("\n");
            last_cat = (*s)->category;
        }
        printf("%-22s  %-12s  %s\n", (*s)->name, (*s)->category, (*s)->summary);
    }
    printf("\nRun '<command> --help' for detailed usage of any command.\n");
}

int main(int argc, char **argv) {
    /* Determine command name from argv[0] (symlink) or argv[1] (dispatcher) */
    const char *progname = strrchr(argv[0], '/');
    progname = progname ? progname + 1 : argv[0];

    /* Dispatcher mode */
    if (strcmp(progname, "aishell") == 0) {
        if (argc < 2) {
            fprintf(stderr, "Usage: aishell <command> [args...]\n");
            fprintf(stderr, "       aishell --list\n\n");
            print_registry();
            return 1;
        }
        if (strcmp(argv[1], "--list") == 0) {
            print_registry();
            return 0;
        }
        /* Shift: argv[1] becomes the new command name */
        progname = argv[1];
        argc--;
        argv++;
    }

    /* Look up command in registry */
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        if (strcmp((*s)->name, progname) == 0)
            return (*s)->run(argc, argv);
    }

    fprintf(stderr, "aishell: unknown command '%s'\n", progname);
    fprintf(stderr, "Run 'aishell --list' to see available commands.\n");
    return 127;
}
