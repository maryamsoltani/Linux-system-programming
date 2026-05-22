#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

#include "cmd_spec.h"
#include "cmd_uname.h"
#include "json_utils.h"

int uname_run(int argc, char **argv)
{
    int json = 0;
    int all = 0;
    int show_s = 0, show_n = 0, show_r = 0, show_v = 0, show_m = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uname_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            all = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            show_s = 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            show_n = 1;
        } else if (strcmp(argv[i], "-r") == 0) {
            show_r = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            show_v = 1;
        } else if (strcmp(argv[i], "-m") == 0) {
            show_m = 1;
        } else {
            fprintf(stderr, "uname: invalid option: %s\n", argv[i]);
            uname_print_usage(stderr);
            return 1;
        }
    }

    struct utsname u;
    if (uname(&u) != 0) {
        perror("uname");
        return 1;
    }

    if (json) {
        printf("{\"sysname\":");
        json_print_string(stdout, u.sysname);
        printf(",\"nodename\":");
        json_print_string(stdout, u.nodename);
        printf(",\"release\":");
        json_print_string(stdout, u.release);
        printf(",\"version\":");
        json_print_string(stdout, u.version);
        printf(",\"machine\":");
        json_print_string(stdout, u.machine);
        printf("}\n");
        return 0;
    }

    /* Default: print sysname only unless flags given */
    if (!all && !show_s && !show_n && !show_r && !show_v && !show_m) {
        show_s = 1;
    }

    int need_space = 0;
    if (all || show_s) { if (need_space) putchar(' '); printf("%s", u.sysname);  need_space = 1; }
    if (all || show_n) { if (need_space) putchar(' '); printf("%s", u.nodename); need_space = 1; }
    if (all || show_r) { if (need_space) putchar(' '); printf("%s", u.release);  need_space = 1; }
    if (all || show_v) { if (need_space) putchar(' '); printf("%s", u.version);  need_space = 1; }
    if (all || show_m) { if (need_space) putchar(' '); printf("%s", u.machine);  need_space = 1; }
    putchar('\n');

    return 0;
}

void uname_print_usage(FILE *out)
{
    fprintf(out, "Usage: uname [-a] [-s] [-n] [-r] [-v] [-m] [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print system information.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-a, --all", "print all information");
    fprintf(out, "  %-20s %s\n", "-s", "print kernel name (default)");
    fprintf(out, "  %-20s %s\n", "-n", "print network node name");
    fprintf(out, "  %-20s %s\n", "-r", "print kernel release");
    fprintf(out, "  %-20s %s\n", "-v", "print kernel version");
    fprintf(out, "  %-20s %s\n", "-m", "print machine hardware name");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_uname_spec = {
    .name        = "uname",
    .summary     = "print system information",
    .long_help   = "Print system information such as kernel name and version.",
    .run         = uname_run,
    .print_usage = uname_print_usage,
};

void register_uname_command(void)
{
    register_command(&cmd_uname_spec);
}
