/*
 * registry.c — In-memory command registry.
 *
 * Holds an array of cmd_spec_t pointers registered at startup.
 * The shell calls registry_find() on every user command to dispatch it.
 * The `help` built-in calls registry_list() to show all available commands.
 */

#include <stdio.h>
#include <string.h>
#include "cmd_spec.h"

/* -----------------------------------------------------------------------
 * Internal state
 * ----------------------------------------------------------------------- */
static const cmd_spec_t *registry[REGISTRY_MAX];
static int               registry_count = 0;

/* -----------------------------------------------------------------------
 * registry_register
 * Add a command spec to the registry.
 * Call this once per command at shell startup (before the read/eval loop).
 * ----------------------------------------------------------------------- */
int registry_register(const cmd_spec_t *spec)
{
    if (registry_count >= REGISTRY_MAX) {
        fprintf(stderr, "registry: full — cannot register '%s'\n", spec->name);
        return -1;
    }
    registry[registry_count++] = spec;
    return 0;
}

/* -----------------------------------------------------------------------
 * registry_find
 * Look up a command by name.  Returns NULL if not found.
 * ----------------------------------------------------------------------- */
const cmd_spec_t *registry_find(const char *name)
{
    int i;
    for (i = 0; i < registry_count; i++) {
        if (strcmp(registry[i]->name, name) == 0)
            return registry[i];
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * registry_list
 * Print a formatted table of all registered commands to `out`.
 * Used by the `help` built-in.
 * ----------------------------------------------------------------------- */
void registry_list(FILE *out)
{
    int i;
    fprintf(out, "\nAvailable commands:\n\n");
    fprintf(out, "  %-12s  %s\n", "COMMAND", "DESCRIPTION");
    fprintf(out, "  %-12s  %s\n", "-------", "-----------");
    for (i = 0; i < registry_count; i++) {
        fprintf(out, "  %-12s  %s\n",
                registry[i]->name,
                registry[i]->summary ? registry[i]->summary : "");
    }
    fprintf(out, "\nType '<command> --help' for detailed usage.\n\n");
}
