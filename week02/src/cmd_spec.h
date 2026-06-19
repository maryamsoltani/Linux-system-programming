#ifndef CMD_SPEC_H
#define CMD_SPEC_H

#include <stdio.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * AiShell — Command Anatomy  (CommandAnatomy.ipynb § cmd_spec_t)
 *
 * Every command module must expose exactly ONE cmd_spec_t.
 * The shell dispatcher and --list use this struct to discover commands.
 *
 * Fields
 * ──────
 *  name        CLI name used to invoke the command  ("ls", "cat", ...)
 *  summary     One-line description shown in --list and pkg metadata
 *  long_help   Longer Markdown description (may be NULL)
 *  category    Group for --list display ("filesystem", "search", "edit", ...)
 *  run         Entry point: parse args and execute the command
 *  print_usage Print usage + option glossary to the given stream
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct cmd_spec {
    const char *name;
    const char *summary;
    const char *long_help;
    const char *category;
    int  (*run)(int argc, char **argv);
    void (*print_usage)(FILE *out);
} cmd_spec_t;

/* ── Registry ────────────────────────────────────────────────────────────── */

/* Filesystem */
extern cmd_spec_t spec_ls;
extern cmd_spec_t spec_stat;
extern cmd_spec_t spec_cat;
extern cmd_spec_t spec_head;
extern cmd_spec_t spec_tail;
extern cmd_spec_t spec_cp;
extern cmd_spec_t spec_mv;
extern cmd_spec_t spec_rm;
extern cmd_spec_t spec_mkdir;
extern cmd_spec_t spec_rmdir;
extern cmd_spec_t spec_touch;

/* Shell / environment */
extern cmd_spec_t spec_pwd;
extern cmd_spec_t spec_cd;
extern cmd_spec_t spec_env;
extern cmd_spec_t spec_export;
extern cmd_spec_t spec_unset;
extern cmd_spec_t spec_type;

/* Search */
extern cmd_spec_t spec_rg;

/* Structured editing */
extern cmd_spec_t spec_edit_replace_line;
extern cmd_spec_t spec_edit_insert_line;
extern cmd_spec_t spec_edit_delete_line;
extern cmd_spec_t spec_edit_replace;

/* The global registry table — NULL-terminated */
static cmd_spec_t * const COMMAND_REGISTRY[] = {
    /* filesystem */
    &spec_ls, &spec_stat, &spec_cat, &spec_head, &spec_tail,
    &spec_cp, &spec_mv,   &spec_rm,  &spec_mkdir, &spec_rmdir, &spec_touch,
    /* shell */
    &spec_pwd, &spec_cd, &spec_env, &spec_export, &spec_unset, &spec_type,
    /* search */
    &spec_rg,
    /* editing */
    &spec_edit_replace_line, &spec_edit_insert_line,
    &spec_edit_delete_line,  &spec_edit_replace,
    NULL
};

#endif /* CMD_SPEC_H */
