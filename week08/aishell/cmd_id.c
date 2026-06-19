#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_id.h"
#include "json_utils.h"

int id_run(int argc, char **argv)
{
    int json = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            id_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else {
            fprintf(stderr, "id: invalid option: %s\n", argv[i]);
            id_print_usage(stderr);
            return 1;
        }
    }

    uid_t uid = getuid();
    gid_t gid = getgid();
    struct passwd *pw = getpwuid(uid);
    struct group  *gr = getgrgid(gid);

    const char *uname = (pw != NULL) ? pw->pw_name : "unknown";
    const char *gname = (gr != NULL) ? gr->gr_name : "unknown";

    if (json) {
        printf("{\"uid\":%u,\"user\":", (unsigned)uid);
        json_print_string(stdout, uname);
        printf(",\"gid\":%u,\"group\":", (unsigned)gid);
        json_print_string(stdout, gname);
        printf("}\n");
    } else {
        printf("uid=%u(%s) gid=%u(%s)\n",
               (unsigned)uid, uname, (unsigned)gid, gname);
    }

    return 0;
}

void id_print_usage(FILE *out)
{
    fprintf(out, "Usage: id [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print user and group identity.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_id_spec = {
    .name        = "id",
    .summary     = "print user and group identity",
    .long_help   = "Print the uid and gid of the current user.",
    .run         = id_run,
    .print_usage = id_print_usage,
};

void register_id_command(void)
{
    register_command(&cmd_id_spec);
}
