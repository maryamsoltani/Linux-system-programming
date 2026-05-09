/*
 * cmd_pkg.c — "pkg" package manager command.
 *
 * Subcommands:
 *   pkg list              List all registered commands/packages
 *   pkg build [name]      Generate pkg.json + usage.txt for all or one command
 *   pkg pack  [name]      Bundle a built package into a .tar.gz archive
 *   pkg install <name>    Install a command to ~/.local/bin (symlink to aishell)
 *   pkg install <f.tar.gz> Install from a packed archive
 *   pkg remove  <name>    Uninstall a previously installed command
 *   pkg installed         List installed packages
 *   pkg info  <name>      Show detailed info for one command
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

extern cmd_spec_t spec_pkg;

/* ── path helpers ─────────────────────────────────────────────────────── */

static const char *install_dir(void) {
    static char dir[512];
    const char *home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/.local/bin", home ? home : "/tmp");
    return dir;
}

static const char *config_dir(void) {
    static char dir[512];
    const char *home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/.config/aishell", home ? home : "/tmp");
    return dir;
}

static const char *registry_path(void) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/installed", config_dir());
    return path;
}

/* ── directory creation ───────────────────────────────────────────────── */

static int make_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "pkg: mkdir '%s': %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

/* ── JSON string writer ───────────────────────────────────────────────── */

static void write_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        if      (*s == '"')  fprintf(f, "\\\"");
        else if (*s == '\\') fprintf(f, "\\\\");
        else if (*s == '\n') fprintf(f, "\\n");
        else                 fputc(*s, f);
    }
    fputc('"', f);
}

/* ── installed-package tracking ───────────────────────────────────────── */

static int is_installed(const char *name) {
    FILE *f = fopen(registry_path(), "r");
    if (!f) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, name) == 0) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static void track_install(const char *name) {
    if (is_installed(name)) return;
    make_dir(config_dir());
    FILE *f = fopen(registry_path(), "a");
    if (!f) return;
    fprintf(f, "%s\n", name);
    fclose(f);
}

static void track_remove(const char *name) {
    const char *rpath = registry_path();
    FILE *f = fopen(rpath, "r");
    if (!f) return;

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", rpath);
    FILE *tmp = fopen(tmp_path, "w");
    if (!tmp) { fclose(f); return; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char trimmed[256];
        strncpy(trimmed, line, sizeof(trimmed) - 1);
        trimmed[strcspn(trimmed, "\n")] = '\0';
        if (strcmp(trimmed, name) != 0)
            fputs(line, tmp);
    }
    fclose(f);
    fclose(tmp);
    rename(tmp_path, rpath);
}

/* ── pkg list ─────────────────────────────────────────────────────────── */

static int pkg_list(void) {
    printf("\nAvailable packages:\n\n");
    printf("  %-22s  %-12s  %-10s  %s\n", "NAME", "CATEGORY", "STATUS", "SUMMARY");
    printf("  %-22s  %-12s  %-10s  %s\n", "----", "--------", "------", "-------");

    const char *last_cat = "";
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        const char *cat = (*s)->category ? (*s)->category : "";
        if (strcmp(cat, last_cat) != 0) {
            printf("\n");
            last_cat = cat;
        }
        const char *status = is_installed((*s)->name) ? "installed" : "-";
        printf("  %-22s  %-12s  %-10s  %s\n",
               (*s)->name, cat, status,
               (*s)->summary ? (*s)->summary : "");
    }
    printf("\n");
    return 0;
}

/* ── pkg installed ────────────────────────────────────────────────────── */

static int pkg_installed(void) {
    FILE *f = fopen(registry_path(), "r");
    if (!f) {
        printf("No packages installed.\n");
        return 0;
    }

    const char *idir = install_dir();
    printf("\nInstalled packages:\n\n");
    printf("  %-22s  %s\n", "NAME", "PATH");
    printf("  %-22s  %s\n", "----", "----");

    char line[256];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;
        char link_path[512];
        snprintf(link_path, sizeof(link_path), "%s/%s", idir, line);
        printf("  %-22s  %s\n", line, link_path);
        count++;
    }
    fclose(f);

    if (count == 0) printf("  (none)\n");
    printf("\n");
    return 0;
}

/* ── pkg info ─────────────────────────────────────────────────────────── */

static int pkg_info(const char *name) {
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        if (strcmp((*s)->name, name) != 0) continue;
        printf("\nPackage:     %s\n", (*s)->name);
        printf("Category:    %s\n",  (*s)->category ? (*s)->category : "");
        printf("Summary:     %s\n",  (*s)->summary  ? (*s)->summary  : "");
        printf("Status:      %s\n",  is_installed((*s)->name) ? "installed" : "not installed");
        if ((*s)->long_help)
            printf("Description: %s\n", (*s)->long_help);
        if ((*s)->print_usage) {
            printf("\n--- Usage ---\n");
            (*s)->print_usage(stdout);
        }
        return 0;
    }
    fprintf(stderr, "pkg info: unknown package '%s'\n", name);
    return 1;
}

/* ── pkg build helpers ────────────────────────────────────────────────── */

static int build_one(const cmd_spec_t *spec) {
    char dir[512], json_path[512], usage_path[512];
    snprintf(dir,        sizeof(dir),        "packages/%s",           spec->name);
    snprintf(json_path,  sizeof(json_path),  "packages/%s/pkg.json",  spec->name);
    snprintf(usage_path, sizeof(usage_path), "packages/%s/usage.txt", spec->name);

    if (make_dir("packages") != 0) return 1;
    if (make_dir(dir)        != 0) return 1;

    FILE *f = fopen(json_path, "w");
    if (!f) {
        fprintf(stderr, "pkg build: cannot write '%s': %s\n", json_path, strerror(errno));
        return 1;
    }
    fprintf(f, "{\n");
    fprintf(f, "  \"name\": ");         write_json_string(f, spec->name);                            fprintf(f, ",\n");
    const char *ver = spec->version ? spec->version : "1.0.0";
    fprintf(f, "  \"version\": "); write_json_string(f, ver); fprintf(f, ",\n");
    fprintf(f, "  \"category\": ");    write_json_string(f, spec->category ? spec->category : "");  fprintf(f, ",\n");
    fprintf(f, "  \"summary\": ");     write_json_string(f, spec->summary  ? spec->summary  : "");  fprintf(f, ",\n");
    fprintf(f, "  \"description\": "); write_json_string(f, spec->long_help? spec->long_help : ""); fprintf(f, "\n");
    fprintf(f, "}\n");
    fclose(f);

    f = fopen(usage_path, "w");
    if (!f) {
        fprintf(stderr, "pkg build: cannot write '%s': %s\n", usage_path, strerror(errno));
        return 1;
    }
    if (spec->print_usage) spec->print_usage(f);
    fclose(f);

    printf("  built: %-22s  →  %s/\n", spec->name, dir);
    return 0;
}

static int pkg_build(const char *name) {
    if (name) {
        for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
            if (strcmp((*s)->name, name) == 0) return build_one(*s);
        }
        fprintf(stderr, "pkg build: unknown package '%s'\n", name);
        return 1;
    }
    printf("\nBuilding all packages:\n\n");
    int errors = 0;
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++)
        errors += build_one(*s);
    printf(errors == 0 ? "\nDone.\n\n" : "\nCompleted with errors.\n\n");
    return errors > 0 ? 1 : 0;
}

/* ── pkg pack ─────────────────────────────────────────────────────────── */

static int pack_one(const char *name) {
    char dir[512];
    snprintf(dir, sizeof(dir), "packages/%s", name);

    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "pkg pack: '%s' not found — run 'pkg build %s' first\n", dir, name);
        return 1;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -czf packages/%s.tar.gz -C packages %s/", name, name);
    printf("  packing: %-22s  →  packages/%s.tar.gz\n", name, name);

    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "pkg pack: tar failed for '%s' (exit %d)\n", name, rc);
        return 1;
    }
    return 0;
}

static int pkg_pack(const char *name) {
    if (name) return pack_one(name);
    printf("\nPacking all packages:\n\n");
    int errors = 0;
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++)
        errors += pack_one((*s)->name);
    printf(errors == 0 ? "\nDone.\n\n" : "\nCompleted with errors.\n\n");
    return errors > 0 ? 1 : 0;
}

/* ── pkg install helpers ──────────────────────────────────────────────── */

/* Create symlink ~/.local/bin/<name> → /path/to/aishell */
static int install_builtin(const char *name) {
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) {
        perror("pkg install: readlink /proc/self/exe");
        return 1;
    }
    exe_path[len] = '\0';

    const char *idir = install_dir();
    make_dir(idir);

    char link_path[512];
    snprintf(link_path, sizeof(link_path), "%s/%s", idir, name);

    unlink(link_path);  /* remove existing if any */

    if (symlink(exe_path, link_path) != 0) {
        fprintf(stderr, "pkg install: symlink '%s': %s\n", link_path, strerror(errno));
        return 1;
    }

    track_install(name);
    printf("  installed: %s\n", link_path);
    printf("           → %s\n\n", exe_path);

    const char *path_env = getenv("PATH");
    if (!path_env || !strstr(path_env, idir))
        printf("  Tip: add '%s' to PATH:\n    export PATH=\"%s:$PATH\"\n\n", idir, idir);

    return 0;
}

/* Extract a .tar.gz, read name from pkg.json, install as builtin */
static int install_from_tar(const char *tarpath) {
    /* Reject paths with shell-unsafe characters */
    for (const char *p = tarpath; *p; p++) {
        if (*p == '\'' || *p == '`' || *p == '$' || *p == ';' || *p == '&') {
            fprintf(stderr, "pkg install: unsafe characters in path\n");
            return 1;
        }
    }

    char tmpdir[] = "/tmp/aishell_install_XXXXXX";
    if (!mkdtemp(tmpdir)) {
        perror("pkg install: mkdtemp");
        return 1;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s' 2>/dev/null", tarpath, tmpdir);
    if (system(cmd) != 0) {
        fprintf(stderr, "pkg install: failed to extract '%s'\n", tarpath);
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
        system(cmd);
        return 1;
    }

    /* Find pkg.json inside the extracted archive */
    char json_path[512] = "";
    snprintf(cmd, sizeof(cmd), "find '%s' -name 'pkg.json' 2>/dev/null | head -1", tmpdir);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        if (!fgets(json_path, sizeof(json_path), fp)) json_path[0] = '\0';
        pclose(fp);
    }
    json_path[strcspn(json_path, "\n")] = '\0';

    if (json_path[0] == '\0') {
        fprintf(stderr, "pkg install: no pkg.json found in archive\n");
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
        system(cmd);
        return 1;
    }

    /* Parse the "name" field from pkg.json */
    char pkg_name[256] = "";
    FILE *jf = fopen(json_path, "r");
    if (jf) {
        char line[512];
        while (fgets(line, sizeof(line), jf)) {
            char *p = strstr(line, "\"name\"");
            if (p) {
                p = strchr(p, ':');
                if (p++) {
                    while (*p == ' ' || *p == '"') p++;
                    int i = 0;
                    while (*p && *p != '"' && i < (int)sizeof(pkg_name) - 1)
                        pkg_name[i++] = *p++;
                    pkg_name[i] = '\0';
                }
                break;
            }
        }
        fclose(jf);
    }

    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
    system(cmd);

    if (pkg_name[0] == '\0') {
        fprintf(stderr, "pkg install: could not read package name from pkg.json\n");
        return 1;
    }

    /* Verify the name is in the registry */
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        if (strcmp((*s)->name, pkg_name) == 0) {
            printf("  Package: %s (from archive)\n", pkg_name);
            return install_builtin(pkg_name);
        }
    }

    fprintf(stderr, "pkg install: package '%s' is not a registered command\n", pkg_name);
    return 1;
}

static int pkg_install(const char *name) {
    if (!name) {
        fprintf(stderr, "pkg install: requires a package name or .tar.gz path\n");
        fprintf(stderr, "Run 'pkg list' to see available packages.\n");
        return 1;
    }

    size_t nlen = strlen(name);
    if (nlen > 7 && strcmp(name + nlen - 7, ".tar.gz") == 0)
        return install_from_tar(name);

    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        if (strcmp((*s)->name, name) == 0)
            return install_builtin(name);
    }

    fprintf(stderr, "pkg install: unknown package '%s'\n", name);
    fprintf(stderr, "Run 'pkg list' to see available packages.\n");
    return 1;
}

/* ── pkg version ──────────────────────────────────────────────────────── */

static int pkg_version(const char *name) {
    if (!name) {
        fprintf(stderr, "pkg version: requires a package name\n");
        return 1;
    }
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        if (strcmp((*s)->name, name) == 0) {
            printf("%s %s\n", (*s)->name, (*s)->version ? (*s)->version : "1.0.0");
            return 0;
        }
    }
    fprintf(stderr, "pkg version: unknown package '%s'\n", name);
    return 1;
}

/* ── pkg remove ───────────────────────────────────────────────────────── */

static int pkg_remove(const char *name) {
    if (!name) {
        fprintf(stderr, "pkg remove: requires a package name\n");
        return 1;
    }
    if (!is_installed(name)) {
        fprintf(stderr, "pkg remove: '%s' is not installed\n", name);
        return 1;
    }

    char link_path[512];
    snprintf(link_path, sizeof(link_path), "%s/%s", install_dir(), name);

    if (unlink(link_path) != 0 && errno != ENOENT) {
        fprintf(stderr, "pkg remove: cannot remove '%s': %s\n", link_path, strerror(errno));
        return 1;
    }

    track_remove(name);
    printf("  removed: %s\n", link_path);
    return 0;
}

/* ── pkg search ───────────────────────────────────────────────────────── */

static int pkg_search(const char *keyword) {
    if (!keyword) {
        fprintf(stderr, "pkg search: requires a keyword\n");
        return 1;
    }

    printf("\nSearch results for '%s':\n\n", keyword);
    printf("  %-22s  %-12s  %-10s  %s\n", "NAME", "CATEGORY", "STATUS", "SUMMARY");
    printf("  %-22s  %-12s  %-10s  %s\n", "----", "--------", "------", "-------");

    int found = 0;
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        const char *name     = (*s)->name     ? (*s)->name     : "";
        const char *summary  = (*s)->summary  ? (*s)->summary  : "";
        const char *cat      = (*s)->category ? (*s)->category : "";
        const char *longhelp = (*s)->long_help? (*s)->long_help: "";

        /* Case-insensitive match against name, summary, category, long_help */
        char kw[256], n[256], s2[512], c[128], lh[1024];
        strncpy(kw, keyword, sizeof(kw) - 1); kw[sizeof(kw)-1] = '\0';
        strncpy(n,  name,    sizeof(n)  - 1); n[sizeof(n)-1]   = '\0';
        strncpy(s2, summary, sizeof(s2) - 1); s2[sizeof(s2)-1] = '\0';
        strncpy(c,  cat,     sizeof(c)  - 1); c[sizeof(c)-1]   = '\0';
        strncpy(lh, longhelp,sizeof(lh) - 1); lh[sizeof(lh)-1] = '\0';

        for (char *p = kw; *p; p++) *p = (char)tolower((unsigned char)*p);
        for (char *p = n;  *p; p++) *p = (char)tolower((unsigned char)*p);
        for (char *p = s2; *p; p++) *p = (char)tolower((unsigned char)*p);
        for (char *p = c;  *p; p++) *p = (char)tolower((unsigned char)*p);
        for (char *p = lh; *p; p++) *p = (char)tolower((unsigned char)*p);

        if (strstr(n, kw) || strstr(s2, kw) || strstr(c, kw) || strstr(lh, kw)) {
            const char *status = is_installed((*s)->name) ? "installed" : "-";
            printf("  %-22s  %-12s  %-10s  %s\n",
                   (*s)->name, (*s)->category ? (*s)->category : "",
                   status, (*s)->summary ? (*s)->summary : "");
            found++;
        }
    }

    if (found == 0)
        printf("  No packages found matching '%s'.\n", keyword);
    printf("\n");
    return 0;
}

/* ── pkg update ───────────────────────────────────────────────────────── */

static int update_one(const char *name) {
    if (!is_installed(name)) {
        fprintf(stderr, "pkg update: '%s' is not installed — run 'pkg install %s' first\n",
                name, name);
        return 1;
    }

    /* Re-create symlink to pick up new binary path */
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0) { perror("pkg update: readlink"); return 1; }
    exe_path[len] = '\0';

    char link_path[512];
    snprintf(link_path, sizeof(link_path), "%s/%s", install_dir(), name);
    unlink(link_path);

    if (symlink(exe_path, link_path) != 0) {
        fprintf(stderr, "pkg update: symlink '%s': %s\n", link_path, strerror(errno));
        return 1;
    }

    /* Rebuild package metadata */
    for (cmd_spec_t * const *s = COMMAND_REGISTRY; *s; s++) {
        if (strcmp((*s)->name, name) == 0) {
            build_one(*s);
            printf("  updated:  %-22s  version: %s\n",
                   name, (*s)->version ? (*s)->version : "1.0.0");
            return 0;
        }
    }
    return 0;
}

static int pkg_update(const char *name) {
    if (name)
        return update_one(name);

    /* Update all installed packages */
    FILE *f = fopen(registry_path(), "r");
    if (!f) {
        printf("No packages installed.\n");
        return 0;
    }

    printf("\nUpdating all installed packages:\n\n");
    char line[256];
    int errors = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;
        errors += update_one(line);
    }
    fclose(f);
    printf(errors == 0 ? "\nAll packages up to date.\n\n" : "\nCompleted with errors.\n\n");
    return errors > 0 ? 1 : 0;
}

/* ── pkg pip ──────────────────────────────────────────────────────────── */

static int pkg_pip(int argc, char **argv) {
    /* argv[0]="pkg" argv[1]="pip" argv[2]=pip-subcommand ... */
    if (argc < 3) {
        printf("Usage: pkg pip <pip-subcommand> [args...]\n\n");
        printf("Examples:\n");
        printf("  pkg pip install requests\n");
        printf("  pkg pip install numpy pandas\n");
        printf("  pkg pip uninstall requests\n");
        printf("  pkg pip list\n");
        printf("  pkg pip show requests\n");
        printf("  pkg pip search flask\n\n");
        return 0;
    }

    /* Check pip is available */
    if (system("pip --version > /dev/null 2>&1") != 0 &&
        system("pip3 --version > /dev/null 2>&1") != 0) {
        fprintf(stderr, "pkg pip: pip is not installed or not in PATH\n");
        fprintf(stderr, "Install it with: sudo apt install python3-pip\n");
        return 1;
    }

    /* Determine pip binary name */
    const char *pip_bin = (system("pip --version > /dev/null 2>&1") == 0) ? "pip" : "pip3";

    /* Build the pip command from remaining argv */
    char cmd[2048];
    int pos = snprintf(cmd, sizeof(cmd), "%s", pip_bin);
    for (int i = 2; i < argc && pos < (int)sizeof(cmd) - 2; i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", argv[i]);

    printf("  running: %s\n\n", cmd);
    return system(cmd);
}

/* ── print_usage ──────────────────────────────────────────────────────── */

void pkg_print_usage(FILE *out) {
    struct arg_lit *opt_h  = arg_lit0("h", "help", "display this help and exit");
    struct arg_str *subcmd = arg_str0(NULL, NULL,
        "list|search|build|pack|install|update|remove|installed|version|pip|info", "subcommand");
    struct arg_str *name   = arg_str0(NULL, NULL, "[name|keyword|file.tar.gz]", "package name, keyword, or archive");
    struct arg_end *end    = arg_end(10);
    void *argtable[] = { opt_h, subcmd, name, end };

    fprintf(out, "Usage: pkg");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n", spec_pkg.long_help);
    fprintf(out, "\nSubcommands:\n");
    fprintf(out, "  %-30s  %s\n", "list",                    "list all packages and install status");
    fprintf(out, "  %-30s  %s\n", "search <keyword>",        "search packages by name, category, or description");
    fprintf(out, "  %-30s  %s\n", "build [name]",            "generate pkg.json + usage.txt");
    fprintf(out, "  %-30s  %s\n", "pack  [name]",            "bundle package into .tar.gz");
    fprintf(out, "  %-30s  %s\n", "install <name>",          "install command to ~/.local/bin");
    fprintf(out, "  %-30s  %s\n", "install <file.tar.gz>",   "install from packed archive");
    fprintf(out, "  %-30s  %s\n", "update [name]",           "update one or all installed packages");
    fprintf(out, "  %-30s  %s\n", "remove  <name>",          "uninstall command");
    fprintf(out, "  %-30s  %s\n", "installed",               "list installed packages");
    fprintf(out, "  %-30s  %s\n", "version <name>",          "show version of a package");
    fprintf(out, "  %-30s  %s\n", "pip <subcmd> [args...]",  "run pip to install Python packages");
    fprintf(out, "  %-30s  %s\n", "info    <name>",          "show full details for one package");
    fprintf(out, "\nOptions:\n");
    arg_print_glossary(out, argtable, "  %-30s %s\n");
    fprintf(out, "\nTypical workflow:\n");
    fprintf(out, "  pkg search file                # search packages by keyword\n");
    fprintf(out, "  pkg build ls                   # generate metadata for ls\n");
    fprintf(out, "  pkg pack  ls                   # bundle into packages/ls.tar.gz\n");
    fprintf(out, "  pkg install ls                 # install ls to ~/.local/bin\n");
    fprintf(out, "  pkg install packages/ls.tar.gz # install from archive\n");
    fprintf(out, "  pkg update ls                  # update ls to latest binary\n");
    fprintf(out, "  pkg update                     # update all installed packages\n");
    fprintf(out, "  pkg installed                  # list what is installed\n");
    fprintf(out, "  pkg remove ls                  # uninstall ls\n");
    fprintf(out, "  pkg pip install requests       # install Python package via pip\n");
    fprintf(out, "  pkg pip list                   # list installed Python packages\n\n");
    arg_free(argtable);
}

/* ── run ──────────────────────────────────────────────────────────────── */

int pkg_run(int argc, char **argv) {
    /* Handle 'pip' early — passes raw argv to pip, bypassing argtable */
    if (argc >= 2 && strcmp(argv[1], "pip") == 0)
        return pkg_pip(argc, argv);

    struct arg_lit *opt_h  = arg_lit0("h", "help", "display this help and exit");
    struct arg_str *subcmd = arg_str0(NULL, NULL,
        "list|search|build|pack|install|update|remove|installed|version|pip|info", "subcommand");
    struct arg_str *name   = arg_str0(NULL, NULL, "[name|file.tar.gz]", "package name or archive");
    struct arg_end *end    = arg_end(10);
    void *argtable[] = { opt_h, subcmd, name, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (opt_h->count > 0) { pkg_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) {
        arg_print_errors(stderr, end, "pkg");
        fprintf(stderr, "Try 'pkg --help' for more information.\n");
        arg_free(argtable); return 1;
    }
    if (subcmd->count == 0) { pkg_print_usage(stdout); arg_free(argtable); return 0; }

    const char *sub = subcmd->sval[0];
    const char *n   = name->count > 0 ? name->sval[0] : NULL;
    arg_free(argtable);

    if (strcmp(sub, "list")      == 0) return pkg_list();
    if (strcmp(sub, "search")    == 0) return pkg_search(n);
    if (strcmp(sub, "build")     == 0) return pkg_build(n);
    if (strcmp(sub, "pack")      == 0) return pkg_pack(n);
    if (strcmp(sub, "install")   == 0) return pkg_install(n);
    if (strcmp(sub, "update")    == 0) return pkg_update(n);
    if (strcmp(sub, "remove")    == 0) return pkg_remove(n);
    if (strcmp(sub, "installed") == 0) return pkg_installed();
    if (strcmp(sub, "version")   == 0) return pkg_version(n);
    if (strcmp(sub, "info")      == 0) {
        if (!n) { fprintf(stderr, "pkg info: requires a package name\n"); return 1; }
        return pkg_info(n);
    }

    fprintf(stderr, "pkg: unknown subcommand '%s'\n", sub);
    fprintf(stderr, "Try 'pkg --help' for more information.\n");
    return 1;
}

/* ── spec ─────────────────────────────────────────────────────────────── */

cmd_spec_t spec_pkg = {
    .name        = "pkg",
    .summary     = "install, remove, and manage shell command packages",
    .long_help   = "Full package manager for aishell commands.\n"
                   "Build metadata, pack archives, install to ~/.local/bin, and track installed packages.",
    .category    = "package",
    .run         = pkg_run,
    .print_usage = pkg_print_usage,
};
