#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

/* ════════════════════════════════════════════════════════════════════════════
 * Shared helpers
 * ════════════════════════════════════════════════════════════════════════════ */

#define BUF_SIZE 65536

static int copy_file(const char *src, const char *dst) {
    int fdin = open(src, O_RDONLY);
    if (fdin < 0) { perror(src); return 1; }
    struct stat st; fstat(fdin, &st);
    int fdout = open(dst, O_WRONLY|O_CREAT|O_TRUNC, st.st_mode & 0777);
    if (fdout < 0) { perror(dst); close(fdin); return 1; }
    char buf[BUF_SIZE]; ssize_t n;
    while ((n = read(fdin, buf, BUF_SIZE)) > 0)
        if (write(fdout, buf, n) != n) { perror(dst); close(fdin); close(fdout); return 1; }
    close(fdin); close(fdout); return 0;
}

static int copy_recursive(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st) != 0) { perror(src); return 1; }
    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, st.st_mode & 0777);
        DIR *dir = opendir(src);
        if (!dir) { perror(src); return 1; }
        struct dirent *e; int ret = 0;
        while ((e = readdir(dir))) {
            if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
            char ns[4096], nd[4096];
            snprintf(ns,sizeof(ns),"%s/%s",src,e->d_name);
            snprintf(nd,sizeof(nd),"%s/%s",dst,e->d_name);
            ret |= copy_recursive(ns, nd);
        }
        closedir(dir); return ret;
    }
    return copy_file(src, dst);
}

static int rm_recursive(const char *path, int force) {
    struct stat st;
    if (lstat(path, &st) != 0) { if (!force) perror(path); return force?0:1; }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) { if (!force) perror(path); return force?0:1; }
        struct dirent *e; int ret = 0;
        while ((e = readdir(dir))) {
            if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
            char child[4096];
            snprintf(child,sizeof(child),"%s/%s",path,e->d_name);
            ret |= rm_recursive(child, force);
        }
        closedir(dir);
        if (rmdir(path)!=0) { if (!force) perror(path); return force?0:1; }
        return ret;
    }
    if (unlink(path)!=0) { if (!force) perror(path); return force?0:1; }
    return 0;
}

static int mkdir_parents(const char *path, mode_t mode) {
    char tmp[4096]; snprintf(tmp,sizeof(tmp),"%s",path);
    size_t len = strlen(tmp);
    if (len && tmp[len-1]=='/') tmp[--len]='\0';
    for (size_t i=1; i<len; i++) {
        if (tmp[i]=='/') {
            tmp[i]='\0';
            if (mkdir(tmp,mode)!=0 && errno!=EEXIST) { perror(tmp); return 1; }
            tmp[i]='/';
        }
    }
    if (mkdir(tmp,mode)!=0 && errno!=EEXIST) { perror(tmp); return 1; }
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * cp
 * ════════════════════════════════════════════════════════════════════════════ */

void cp_print_usage(FILE *out) {
    struct arg_lit *opt_r = arg_lit0("r","recursive","copy directories recursively");
    struct arg_lit *opt_f = arg_lit0("f","force","force overwrite existing destination");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON success/error summary");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_p = arg_strn(NULL,NULL,"SRC/DST",2,100,"source(s) then destination");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_r, opt_f, opt_j, opt_h, opt_p, end };
    fprintf(out,"Usage: cp"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_cp.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int cp_run(int argc, char **argv) {
    struct arg_lit *opt_r = arg_lit0("r","recursive","copy directories recursively");
    struct arg_lit *opt_f = arg_lit0("f","force","force overwrite existing destination");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON success/error summary");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_p = arg_strn(NULL,NULL,"SRC/DST",2,100,"source(s) then destination");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_r, opt_f, opt_j, opt_h, opt_p, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { cp_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"cp"); arg_free(argtable); return 1; }

    const char *dst = opt_p->sval[opt_p->count-1];
    int json = opt_j->count>0, ret = 0;

    for (int i=0; i<opt_p->count-1; i++) {
        const char *src = opt_p->sval[i];
        struct stat st_src, st_dst;
        int dst_is_dir = (stat(dst,&st_dst)==0 && S_ISDIR(st_dst.st_mode));
        char actual_dst[4096];
        if (dst_is_dir) {
            const char *base = strrchr(src,'/'); base = base?base+1:src;
            snprintf(actual_dst,sizeof(actual_dst),"%s/%s",dst,base);
        } else snprintf(actual_dst,sizeof(actual_dst),"%s",dst);

        int r = 0;
        if (lstat(src,&st_src)==0 && S_ISDIR(st_src.st_mode)) {
            if (!opt_r->count) {
                fprintf(stderr,"cp: -r not specified; omitting directory '%s'\n",src);
                r = 1;
            } else r = copy_recursive(src,actual_dst);
        } else r = copy_file(src,actual_dst);

        if (json) printf("{\"src\":\"%s\",\"dst\":\"%s\",\"status\":\"%s\"}\n",
                         src, actual_dst, r==0?"ok":"error");
        ret |= r;
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_cp = {
    .name="cp", .summary="Copy files or directories",
    .long_help="Copy SOURCE to DEST. Use -r for directories, -f to force overwrite.",
    .category="filesystem", .run=cp_run, .print_usage=cp_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * mv
 * ════════════════════════════════════════════════════════════════════════════ */

void mv_print_usage(FILE *out) {
    struct arg_lit *opt_f = arg_lit0("f","force","force overwrite without prompt");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_p = arg_strn(NULL,NULL,"SRC/DST",2,100,"source(s) then destination");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_f, opt_j, opt_h, opt_p, end };
    fprintf(out,"Usage: mv"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_mv.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int mv_run(int argc, char **argv) {
    struct arg_lit *opt_f = arg_lit0("f","force","force overwrite without prompt");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_p = arg_strn(NULL,NULL,"SRC/DST",2,100,"source(s) then destination");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_f, opt_j, opt_h, opt_p, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { mv_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"mv"); arg_free(argtable); return 1; }

    const char *dst = opt_p->sval[opt_p->count-1];
    int json = opt_j->count>0, ret = 0;

    for (int i=0; i<opt_p->count-1; i++) {
        const char *src = opt_p->sval[i];
        struct stat st_dst;
        int dst_is_dir = (stat(dst,&st_dst)==0 && S_ISDIR(st_dst.st_mode));
        char actual_dst[4096];
        if (dst_is_dir) {
            const char *base = strrchr(src,'/'); base = base?base+1:src;
            snprintf(actual_dst,sizeof(actual_dst),"%s/%s",dst,base);
        } else snprintf(actual_dst,sizeof(actual_dst),"%s",dst);

        int r = rename(src,actual_dst)!=0 ? (perror("mv"),1) : 0;
        if (json) printf("{\"src\":\"%s\",\"dst\":\"%s\",\"status\":\"%s\"}\n",
                         src,actual_dst,r==0?"ok":"error");
        ret |= r;
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_mv = {
    .name="mv", .summary="Move or rename files",
    .long_help="Move SOURCE to DEST, or move multiple SOURCEs into DIRECTORY.",
    .category="filesystem", .run=mv_run, .print_usage=mv_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * rm
 * ════════════════════════════════════════════════════════════════════════════ */

void rm_print_usage(FILE *out) {
    struct arg_lit *opt_r = arg_lit0("r","recursive","remove directories recursively");
    struct arg_lit *opt_f = arg_lit0("f","force","ignore nonexistent, never prompt");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_p = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to remove");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_r, opt_f, opt_j, opt_h, opt_p, end };
    fprintf(out,"Usage: rm"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_rm.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int rm_run(int argc, char **argv) {
    struct arg_lit *opt_r = arg_lit0("r","recursive","remove directories recursively");
    struct arg_lit *opt_f = arg_lit0("f","force","ignore nonexistent, never prompt");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_p = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to remove");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_r, opt_f, opt_j, opt_h, opt_p, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { rm_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"rm"); arg_free(argtable); return 1; }

    int recursive=opt_r->count>0, force=opt_f->count>0, json=opt_j->count>0, ret=0;

    for (int i=0; i<opt_p->count; i++) {
        const char *path = opt_p->sval[i];
        struct stat st;
        int r = 0;
        if (lstat(path,&st)!=0) {
            if (!force) { perror(path); r=1; }
        } else if (S_ISDIR(st.st_mode)) {
            if (!recursive) { fprintf(stderr,"rm: cannot remove '%s': Is a directory\n",path); r=1; }
            else r = rm_recursive(path,force);
        } else {
            if (unlink(path)!=0 && !force) { perror(path); r=1; }
        }
        if (json) printf("{\"path\":\"%s\",\"status\":\"%s\"}\n",path,r==0?"ok":"error");
        ret |= r;
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_rm = {
    .name="rm", .summary="Remove files or directories",
    .long_help="Remove FILE(s). Use -r for directories, -f to ignore missing files.",
    .category="filesystem", .run=rm_run, .print_usage=rm_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * mkdir
 * ════════════════════════════════════════════════════════════════════════════ */

void mkdir_print_usage(FILE *out) {
    struct arg_lit *opt_p = arg_lit0("p","parents","no error if existing; create parents as needed");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_d = arg_strn(NULL,NULL,"DIR",1,100,"directory(ies) to create");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_p, opt_j, opt_h, opt_d, end };
    fprintf(out,"Usage: mkdir"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_mkdir.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int mkdir_run(int argc, char **argv) {
    struct arg_lit *opt_p = arg_lit0("p","parents","no error if existing; create parents as needed");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_d = arg_strn(NULL,NULL,"DIR",1,100,"directory(ies) to create");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_p, opt_j, opt_h, opt_d, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { mkdir_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"mkdir"); arg_free(argtable); return 1; }

    int parents=opt_p->count>0, json=opt_j->count>0, ret=0;
    for (int i=0; i<opt_d->count; i++) {
        const char *dir = opt_d->sval[i];
        int r = parents ? mkdir_parents(dir,0755) : (mkdir(dir,0755)!=0?(perror(dir),1):0);
        if (json) printf("{\"path\":\"%s\",\"status\":\"%s\"}\n",dir,r==0?"ok":"error");
        ret |= r;
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_mkdir = {
    .name="mkdir", .summary="Create directories",
    .long_help="Create DIRECTORY(ies). Use -p to create parent directories as needed.",
    .category="filesystem", .run=mkdir_run, .print_usage=mkdir_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * rmdir
 * ════════════════════════════════════════════════════════════════════════════ */

void rmdir_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_d = arg_strn(NULL,NULL,"DIR",1,100,"empty directory(ies) to remove");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_d, end };
    fprintf(out,"Usage: rmdir"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_rmdir.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int rmdir_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_d = arg_strn(NULL,NULL,"DIR",1,100,"empty directory(ies) to remove");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_d, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { rmdir_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"rmdir"); arg_free(argtable); return 1; }

    int json=opt_j->count>0, ret=0;
    for (int i=0; i<opt_d->count; i++) {
        const char *dir = opt_d->sval[i];
        int r = rmdir(dir)!=0?(perror(dir),1):0;
        if (json) printf("{\"path\":\"%s\",\"status\":\"%s\"}\n",dir,r==0?"ok":"error");
        ret |= r;
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_rmdir = {
    .name="rmdir", .summary="Remove empty directories",
    .long_help="Remove empty DIRECTORY(ies).",
    .category="filesystem", .run=rmdir_run, .print_usage=rmdir_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * touch
 * ════════════════════════════════════════════════════════════════════════════ */

void touch_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to touch");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, end };
    fprintf(out,"Usage: touch"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_touch.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int touch_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to touch");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { touch_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"touch"); arg_free(argtable); return 1; }

    int json=opt_j->count>0, ret=0;
    for (int i=0; i<opt_f->count; i++) {
        const char *fname = opt_f->sval[i];
        int fd = open(fname,O_CREAT|O_WRONLY,0644);
        int r = 0;
        if (fd<0) { perror(fname); r=1; }
        else { close(fd); if (utime(fname,NULL)!=0) { perror(fname); r=1; } }
        if (json) printf("{\"path\":\"%s\",\"status\":\"%s\"}\n",fname,r==0?"ok":"error");
        ret |= r;
    }
    arg_free(argtable); return ret;
}

cmd_spec_t spec_touch = {
    .name="touch", .summary="Create file or update timestamp",
    .long_help="Update timestamps of FILE(s) to now. Creates file if it does not exist.",
    .category="filesystem", .run=touch_run, .print_usage=touch_print_usage,
};
