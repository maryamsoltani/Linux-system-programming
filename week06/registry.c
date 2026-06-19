#include <stddef.h>
#include <string.h>
#include "cmd_spec.h"

#define MAX_COMMANDS 32

static const cmd_spec_t *command_registry[MAX_COMMANDS];
static size_t command_count;

void register_command(const cmd_spec_t *spec)
{
    if (spec == NULL) return;
    if (command_count < MAX_COMMANDS)
        command_registry[command_count++] = spec;
}

const cmd_spec_t *find_command(const char *name)
{
    if (name == NULL) return NULL;
    for (size_t i = 0; i < command_count; i++) {
        const cmd_spec_t *s = command_registry[i];
        if (s != NULL && s->name != NULL && strcmp(s->name, name) == 0)
            return s;
    }
    return NULL;
}

void for_each_command(void (*callback)(const cmd_spec_t *spec, void *userdata), void *userdata)
{
    if (callback == NULL) return;
    for (size_t i = 0; i < command_count; i++) {
        if (command_registry[i] != NULL)
            callback(command_registry[i], userdata);
    }
}
