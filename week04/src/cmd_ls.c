#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

/* ─── ls ─────────────────────────────────────────────────────────────────────
 * Anatomy:
 *   ls_print_usage()  — prints help from the same argtable3 definitions
 *   ls_run()          — parses args, dispatches to human or JSON output
 *   spec_ls           — the cmd_spec_t exposed to the registry
 * ───────────────────────────────────────────────────────────────────────── */

static void print_perms(mode_t mode) {
    printf("%c", S_ISDIR(mode) ? 'd' : '-');
    printf("%c", (mode & S_IRUSR) ? 'r' : '-');
    printf("%c", (mode & S_IWUSR) ? 'w' : '-');
    printf("%c", (mode & S_IXUSR) ? 'x' : '-');
    printf("%c", (mode & S_IRGRP) ? 'r' : '-');
    printf("%c", (mode & S_IWGRP) ? 'w' : '-');
    printf("%c", (mode & S_IXGRP) ? 'x' : '-');
    printf("%c", (mode & S_IROTH) ? 'r' : '-');
    printf("%c", (mode & S_IWOTH) ? 'w' : '-');
    printf("%c", (mode & S_IXOTH) ? 'x' : '-');
}

/* ── print_usage ── single source of truth for help text ── */
void ls_print_usage(FILE *out) {
    struct arg_lit *opt_l   = arg_lit0("l", NULL,    "use long listing format");
    struct arg_lit *opt_a   = arg_lit0("a", "all",   "show hidden entries (starting with .)");
    struct arg_lit *opt_j   = arg_lit0(NULL,"json",  "output JSON array of entries");
    struct arg_lit *opt_h   = arg_lit0("h", "help",  "display this help and exit");
    struct arg_str *opt_path= arg_str0(NULL, NULL, "PATH", "directory to list (default: .)");
    struct arg_end *end     = arg_end(10);
    void *argtable[] = { opt_l, opt_a, opt_j, opt_h, opt_path, end };

    fprintf(out, "Usage: ls");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_ls.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

/* ── run ── */
int ls_run(int argc, char **argv) {
    struct arg_lit *opt_l   = arg_lit0("l", NULL,    "use long listing format");
    struct arg_lit *opt_a   = arg_lit0("a", "all",   "show hidden entries (starting with .)");
    struct arg_lit *opt_j   = arg_lit0(NULL,"json",  "output JSON array of entries");
    struct arg_lit *opt_h   = arg_lit0("h", "help",  "display this help and exit");
    struct arg_str *opt_path= arg_str0(NULL, NULL, "PATH", "directory to list (default: .)");
    struct arg_end *end     = arg_end(10);
    void *argtable[] = { opt_l, opt_a, opt_j, opt_h, opt_path, end };

    int nerrors = arg_parse(argc, argv, argtable);

    if (opt_h->count > 0) { ls_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) {
        arg_print_errors(stderr, end, "ls");
        fprintf(stderr, "Try 'ls --help' for more information.\n");
        arg_free(argtable); return 1;
    }

    const char *path  = (opt_path->count > 0) ? opt_path->sval[0] : ".";
    int long_fmt      = opt_l->count > 0;
    int show_all      = opt_a->count > 0;
    int json_mode     = opt_j->count > 0;

    DIR *dir = opendir(path);
    if (!dir) { perror(path); arg_free(argtable); return 1; }

    struct dirent *entry;
    int first = 1;
    if (json_mode) printf("[");

    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.') continue;

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        struct stat st;
        lstat(full, &st);

        if (json_mode) {
            char timebuf[32];
            strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", localtime(&st.st_mtime));
            if (!first) printf(",");
            printf("{\"name\":\"%s\",\"type\":\"%s\",\"size\":%lld,\"mtime\":\"%s\"}",
                entry->d_name,
                S_ISDIR(st.st_mode) ? "directory" : "file",
                (long long)st.st_size, timebuf);
            first = 0;
        } else if (long_fmt) {
            print_perms(st.st_mode);
            struct passwd *pw = getpwuid(st.st_uid);
            struct group  *gr = getgrgid(st.st_gid);
            char timebuf[32];
            strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", localtime(&st.st_mtime));
            printf(" %3lu %-8s %-8s %8lld %s %s\n",
                (unsigned long)st.st_nlink,
                pw ? pw->pw_name : "?", gr ? gr->gr_name : "?",
                (long long)st.st_size, timebuf, entry->d_name);
        } else {
            printf("%s\n", entry->d_name);
        }
    }

    if (json_mode) printf("]\n");
    closedir(dir);
    arg_free(argtable);
    return 0;
}

/* ── cmd_spec_t ── registered in COMMAND_REGISTRY ── */
cmd_spec_t spec_ls = {
    .name        = "ls",
    .summary     = "List directory contents",
    .long_help   = "List files and directories. Supports long format (-l),\n"
                   "hidden files (-a), and JSON output (--json) for agent use.",
    .category    = "filesystem",
    .run         = ls_run,
    .print_usage = ls_print_usage,
};
