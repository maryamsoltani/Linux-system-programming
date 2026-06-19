#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

void stat_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON object");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to stat");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, end };
    fprintf(out, "Usage: stat");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_stat.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

int stat_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON object");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to stat");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (opt_h->count > 0) { stat_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) { arg_print_errors(stderr,end,"stat"); arg_free(argtable); return 1; }

    int json = opt_j->count > 0;
    int ret  = 0;

    for (int i = 0; i < opt_f->count; i++) {
        const char *file = opt_f->sval[i];
        struct stat st;
        if (lstat(file, &st) != 0) { perror(file); ret = 1; continue; }

        const char *type_str = S_ISDIR(st.st_mode)  ? "directory" :
                               S_ISLNK(st.st_mode)  ? "symlink"   :
                               S_ISCHR(st.st_mode)  ? "char-dev"  :
                               S_ISBLK(st.st_mode)  ? "block-dev" :
                               S_ISFIFO(st.st_mode) ? "fifo"      :
                               S_ISSOCK(st.st_mode) ? "socket"    : "file";

        char atbuf[32], mtbuf[32], ctbuf[32];
        strftime(atbuf, sizeof(atbuf), "%Y-%m-%dT%H:%M:%S", localtime(&st.st_atime));
        strftime(mtbuf, sizeof(mtbuf), "%Y-%m-%dT%H:%M:%S", localtime(&st.st_mtime));
        strftime(ctbuf, sizeof(ctbuf), "%Y-%m-%dT%H:%M:%S", localtime(&st.st_ctime));

        if (json) {
            printf("{\"path\":\"%s\",\"type\":\"%s\",\"size\":%lld,"
                   "\"mode\":%o,\"mtime\":\"%s\",\"atime\":\"%s\",\"ctime\":\"%s\"}\n",
                   file, type_str, (long long)st.st_size,
                   st.st_mode & 07777, mtbuf, atbuf, ctbuf);
        } else {
            struct passwd *pw = getpwuid(st.st_uid);
            struct group  *gr = getgrgid(st.st_gid);
            printf("  File: %s\n  Size: %-12lld  Type: %s\n"
                   "  Inode: %-10lu  Links: %lu\n"
                   "  Access: (%04o) Uid: (%d/%s) Gid: (%d/%s)\n"
                   "  Access time: %s\n  Modify time: %s\n  Change time: %s\n",
                   file, (long long)st.st_size, type_str,
                   (unsigned long)st.st_ino, (unsigned long)st.st_nlink,
                   st.st_mode & 07777,
                   st.st_uid, pw ? pw->pw_name : "?",
                   st.st_gid, gr ? gr->gr_name : "?",
                   atbuf, mtbuf, ctbuf);
        }
    }
    arg_free(argtable);
    return ret;
}

cmd_spec_t spec_stat = {
    .name        = "stat",
    .summary     = "Display file/inode metadata",
    .long_help   = "Show file type, size, permissions, owner, and timestamps.\n"
                   "Use --json for structured output suitable for agents.",
    .category    = "filesystem",
    .run         = stat_run,
    .print_usage = stat_print_usage,
};
