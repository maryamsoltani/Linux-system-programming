#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_pkg.h"
#include "json_utils.h"

#define PKG_NAME        "aishell"
#define PKG_VERSION     "1.0.0"
#define PKG_DESCRIPTION "Modular CLI utilities in C"

static void get_base_dir(char *buf, size_t len)
{
    const char *home = getenv("HOME");
    if (home == NULL) home = "/tmp";
    snprintf(buf, len, "%s/.mysh", home);
}

static void get_pkgs_dir(char *buf, size_t len)
{
    char base[1024];
    get_base_dir(base, sizeof(base));
    snprintf(buf, len, "%s/pkgs", base);
}

static void get_bin_dir(char *buf, size_t len)
{
    char base[1024];
    get_base_dir(base, sizeof(base));
    snprintf(buf, len, "%s/bin", base);
}

static void get_db_path(char *buf, size_t len)
{
    char base[1024];
    get_base_dir(base, sizeof(base));
    snprintf(buf, len, "%s/pkgdb.txt", base);
}

static int run_cmd(const char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        perror(argv[0]);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void ensure_dirs(void)
{
    char base[1024], pkgs[1024], bin[1024];
    get_base_dir(base, sizeof(base));
    get_pkgs_dir(pkgs, sizeof(pkgs));
    get_bin_dir(bin, sizeof(bin));
    mkdir(base, 0755);
    mkdir(pkgs, 0755);
    mkdir(bin,  0755);
}

/* pkg build: create a tar.gz of the current directory */
static int pkg_build(int json)
{
    char tarname[256];
    snprintf(tarname, sizeof(tarname), "%s-%s.tar.gz", PKG_NAME, PKG_VERSION);

    /* Write a minimal pkg.json */
    FILE *pf = fopen("pkg.json", "w");
    if (pf) {
        fprintf(pf, "{\"name\":\"%s\",\"version\":\"%s\",\"description\":\"%s\"}\n",
                PKG_NAME, PKG_VERSION, PKG_DESCRIPTION);
        fclose(pf);
    }

    const char *args[] = { "tar", "czf", tarname, ".", NULL };
    int ret = run_cmd(args);

    if (json) {
        printf("{\"command\":\"pkg build\",\"package\":");
        json_print_string(stdout, tarname);
        printf(",\"success\":%s}\n", ret == 0 ? "true" : "false");
    } else {
        if (ret == 0) printf("pkg: built %s\n", tarname);
        else          fprintf(stderr, "pkg: build failed\n");
    }
    return ret;
}

/* pkg install <tarball> */
static int pkg_install(const char *tarball, int json)
{
    ensure_dirs();

    /* create temp dir */
    char tmpdir[1024];
    get_pkgs_dir(tmpdir, sizeof(tmpdir));
    strncat(tmpdir, "/tmp_XXXXXX", sizeof(tmpdir) - strlen(tmpdir) - 1);
    if (mkdtemp(tmpdir) == NULL) {
        perror("mkdtemp");
        return 1;
    }

    /* extract */
    const char *args[] = { "tar", "xzf", tarball, "-C", tmpdir, NULL };
    if (run_cmd(args) != 0) {
        fprintf(stderr, "pkg: failed to extract %s\n", tarball);
        return 1;
    }

    /* read pkg.json */
    char pjpath[2048];
    snprintf(pjpath, sizeof(pjpath), "%s/pkg.json", tmpdir);
    char name[256]    = PKG_NAME;
    char version[64]  = PKG_VERSION;

    FILE *pf = fopen(pjpath, "r");
    if (pf) {
        char line[512];
        while (fgets(line, sizeof(line), pf)) {
            /* naive parse */
            char *p;
            if ((p = strstr(line, "\"name\"")) != NULL) {
                p = strchr(p + 6, '"');
                if (p) {
                    p++;
                    char *end = strchr(p, '"');
                    if (end) { *end = '\0'; snprintf(name, sizeof(name), "%s", p); }
                }
            }
            if ((p = strstr(line, "\"version\"")) != NULL) {
                p = strchr(p + 9, '"');
                if (p) {
                    p++;
                    char *end = strchr(p, '"');
                    if (end) { *end = '\0'; snprintf(version, sizeof(version), "%s", p); }
                }
            }
        }
        fclose(pf);
    }

    /* move to install dir */
    char install_dir[2048];
    char pkgs_dir[1024];
    get_pkgs_dir(pkgs_dir, sizeof(pkgs_dir));
    snprintf(install_dir, sizeof(install_dir), "%s/%s-%s", pkgs_dir, name, version);

    if (rename(tmpdir, install_dir) != 0) {
        fprintf(stderr, "pkg: failed to install to %s: %s\n", install_dir, strerror(errno));
        return 1;
    }

    /* symlink binary if present */
    char bin_src[2048], bin_dst[2048];
    char bin_dir[1024];
    get_bin_dir(bin_dir, sizeof(bin_dir));
    strncpy(bin_src, install_dir, sizeof(bin_src) - 2);
    bin_src[sizeof(bin_src) - 2] = '\0';
    strncat(bin_src, "/", sizeof(bin_src) - strlen(bin_src) - 1);
    strncat(bin_src, name, sizeof(bin_src) - strlen(bin_src) - 1);

    strncpy(bin_dst, bin_dir, sizeof(bin_dst) - 2);
    bin_dst[sizeof(bin_dst) - 2] = '\0';
    strncat(bin_dst, "/", sizeof(bin_dst) - strlen(bin_dst) - 1);
    strncat(bin_dst, name, sizeof(bin_dst) - strlen(bin_dst) - 1);
    unlink(bin_dst);
    symlink(bin_src, bin_dst);

    /* write to pkgdb */
    char db_path[1024];
    get_db_path(db_path, sizeof(db_path));
    FILE *db = fopen(db_path, "a");
    if (db) {
        fprintf(db, "%s %s %s\n", name, version, install_dir);
        fclose(db);
    }

    if (json) {
        printf("{\"command\":\"pkg install\",\"name\":");
        json_print_string(stdout, name);
        printf(",\"version\":");
        json_print_string(stdout, version);
        printf(",\"install_dir\":");
        json_print_string(stdout, install_dir);
        printf(",\"success\":true}\n");
    } else {
        printf("pkg: installed %s-%s to %s\n", name, version, install_dir);
    }

    return 0;
}

/* pkg list */
static int pkg_list(int json)
{
    char db_path[1024];
    get_db_path(db_path, sizeof(db_path));

    FILE *db = fopen(db_path, "r");
    if (!db) {
        if (json) printf("{\"packages\":[]}\n");
        else      printf("pkg: no packages installed\n");
        return 0;
    }

    if (json) printf("{\"packages\":[\n");
    int first = 1;
    char line[1024];
    while (fgets(line, sizeof(line), db)) {
        /* strip newline */
        line[strcspn(line, "\n")] = '\0';
        char name[256], version[64], idir[512];
        if (sscanf(line, "%255s %63s %511s", name, version, idir) < 2) continue;
        if (json) {
            if (!first) printf(",\n");
            printf("  {\"name\":");
            json_print_string(stdout, name);
            printf(",\"version\":");
            json_print_string(stdout, version);
            printf("}");
            first = 0;
        } else {
            printf("%s %s\n", name, version);
        }
    }
    fclose(db);

    if (json) printf("\n]}\n");

    return 0;
}

/* pkg remove <name> */
static int pkg_remove(const char *name, int json)
{
    char db_path[1024];
    get_db_path(db_path, sizeof(db_path));

    FILE *db = fopen(db_path, "r");
    if (!db) {
        fprintf(stderr, "pkg: package database not found\n");
        return 1;
    }

    char lines[256][1024];
    int nlines = 0;
    char found_dir[512] = "";
    char found_ver[64]  = "";

    char line[1024];
    while (fgets(line, sizeof(line), db) && nlines < 256) {
        line[strcspn(line, "\n")] = '\0';
        char pname[256], pver[64], pdir[512];
        if (sscanf(line, "%255s %63s %511s", pname, pver, pdir) < 2) continue;
        if (strcmp(pname, name) == 0) {
            snprintf(found_dir, sizeof(found_dir), "%s", pdir);
            snprintf(found_ver, sizeof(found_ver), "%s", pver);
        } else {
            strncpy(lines[nlines], line, 1022);
            lines[nlines][1022] = '\0';
            strncat(lines[nlines], "\n", 2);
            nlines++;
        }
    }
    fclose(db);

    if (found_dir[0] == '\0') {
        fprintf(stderr, "pkg: package '%s' not found\n", name);
        return 1;
    }

    /* remove install dir */
    const char *rm_args[] = { "rm", "-rf", found_dir, NULL };
    run_cmd(rm_args);

    /* remove symlink */
    char bin_dir[1024], bin_dst[2048];
    get_bin_dir(bin_dir, sizeof(bin_dir));
    snprintf(bin_dst, sizeof(bin_dst), "%s/%s", bin_dir, name);
    unlink(bin_dst);

    /* rewrite db */
    db = fopen(db_path, "w");
    if (db) {
        for (int i = 0; i < nlines; i++) fputs(lines[i], db);
        fclose(db);
    }

    if (json) {
        printf("{\"command\":\"pkg remove\",\"name\":");
        json_print_string(stdout, name);
        printf(",\"version\":");
        json_print_string(stdout, found_ver);
        printf(",\"success\":true}\n");
    } else {
        printf("pkg: removed %s-%s\n", name, found_ver);
    }

    return 0;
}

int pkg_run(int argc, char **argv)
{
    int json = 0;
    int i;

    /* check for --json anywhere */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json = 1;
    }

    if (argc < 2) {
        /* print metadata */
        if (json) {
            printf("{\"name\":\"%s\",\"version\":\"%s\",\"description\":\"%s\"}\n",
                   PKG_NAME, PKG_VERSION, PKG_DESCRIPTION);
        } else {
            printf("Name:        %s\n", PKG_NAME);
            printf("Version:     %s\n", PKG_VERSION);
            printf("Description: %s\n", PKG_DESCRIPTION);
        }
        return 0;
    }

    /* find subcommand (skip --json) */
    const char *subcmd = NULL;
    int sub_arg_start = argc;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) continue;
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            pkg_print_usage(stdout);
            return 0;
        }
        if (argv[i][0] != '-') {
            subcmd = argv[i];
            sub_arg_start = i + 1;
            break;
        }
    }

    if (subcmd == NULL) {
        /* just --json or unknown flags: print metadata */
        if (json) {
            printf("{\"name\":\"%s\",\"version\":\"%s\",\"description\":\"%s\"}\n",
                   PKG_NAME, PKG_VERSION, PKG_DESCRIPTION);
        } else {
            printf("Name:        %s\n", PKG_NAME);
            printf("Version:     %s\n", PKG_VERSION);
            printf("Description: %s\n", PKG_DESCRIPTION);
        }
        return 0;
    }

    if (strcmp(subcmd, "build") == 0) {
        return pkg_build(json);
    } else if (strcmp(subcmd, "install") == 0) {
        if (sub_arg_start >= argc) {
            fprintf(stderr, "pkg install: requires a tarball argument\n");
            return 1;
        }
        const char *tarball = NULL;
        for (i = sub_arg_start; i < argc; i++) {
            if (strcmp(argv[i], "--json") != 0) { tarball = argv[i]; break; }
        }
        if (!tarball) { fprintf(stderr, "pkg install: requires a tarball argument\n"); return 1; }
        return pkg_install(tarball, json);
    } else if (strcmp(subcmd, "list") == 0) {
        return pkg_list(json);
    } else if (strcmp(subcmd, "remove") == 0) {
        if (sub_arg_start >= argc) {
            fprintf(stderr, "pkg remove: requires a package name\n");
            return 1;
        }
        const char *pkgname = NULL;
        for (i = sub_arg_start; i < argc; i++) {
            if (strcmp(argv[i], "--json") != 0) { pkgname = argv[i]; break; }
        }
        if (!pkgname) { fprintf(stderr, "pkg remove: requires a package name\n"); return 1; }
        return pkg_remove(pkgname, json);
    } else {
        fprintf(stderr, "pkg: unknown subcommand: %s\n", subcmd);
        pkg_print_usage(stderr);
        return 1;
    }
}

void pkg_print_usage(FILE *out)
{
    fprintf(out, "Usage: pkg [build|install TARBALL|list|remove NAME] [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Package manager for %s.\n", PKG_NAME);
    fprintf(out, "\nSubcommands:\n");
    fprintf(out, "  %-20s %s\n", "build",          "create a .tar.gz package");
    fprintf(out, "  %-20s %s\n", "install TARBALL", "install from a tarball");
    fprintf(out, "  %-20s %s\n", "list",            "list installed packages");
    fprintf(out, "  %-20s %s\n", "remove NAME",     "remove an installed package");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json",     "output in JSON format");
}

static cmd_spec_t cmd_pkg_spec = {
    .name        = "pkg",
    .summary     = "package manager",
    .long_help   = "Build, install, list, and remove packages.",
    .run         = pkg_run,
    .print_usage = pkg_print_usage,
};

void register_pkg_command(void)
{
    register_command(&cmd_pkg_spec);
}
