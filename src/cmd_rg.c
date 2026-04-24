#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

/* ─── rg — regex text search ─────────────────────────────────────────────── */

static int g_json        = 0;
static int g_line_nums   = 0;
static int g_ignore_case = 0;
static int g_whole_word  = 0;
static int g_context     = 0;
static int g_fixed       = 0;

static void search_file(const char *path, regex_t *re,
                        const char *raw_pattern) {
    FILE *fp = fopen(path,"r");
    if (!fp) return;

    char **lines   = NULL;
    int   capacity = 0, count = 0;
    char  buf[4096];

    while (fgets(buf,sizeof(buf),fp)) {
        if (count >= capacity) {
            capacity = capacity ? capacity*2 : 64;
            lines = realloc(lines, capacity * sizeof(char*));
        }
        lines[count++] = strdup(buf);
    }
    fclose(fp);

    for (int i=0; i<count; i++) {
        int matched = 0;
        if (g_fixed) {
            matched = strstr(lines[i], raw_pattern) != NULL;
        } else {
            regmatch_t m;
            matched = regexec(re, lines[i], 1, &m, 0)==0;
        }
        if (!matched) continue;

        char *line_stripped = strdup(lines[i]);
        line_stripped[strcspn(line_stripped,"\n")] = '\0';

        if (g_json) {
            printf("{\"file\":\"%s\",\"line\":%d,\"text\":\"%s\"}\n",
                   path, i+1, line_stripped);
        } else {
            /* context: print preceding lines */
            if (g_context > 0) {
                int start = i - g_context;
                if (start < 0) start = 0;
                for (int c=start; c<i; c++) {
                    char *cl = strdup(lines[c]);
                    cl[strcspn(cl,"\n")]='\0';
                    if (g_line_nums) printf("%s-%d-%s\n",path,c+1,cl);
                    else             printf("%s-%s\n",path,cl);
                    free(cl);
                }
            }
            if (g_line_nums) printf("%s:%d:%s\n", path, i+1, line_stripped);
            else             printf("%s:%s\n", path, line_stripped);
            /* context: print following lines */
            if (g_context > 0) {
                int end = i + g_context;
                if (end >= count) end = count-1;
                for (int c=i+1; c<=end; c++) {
                    char *cl = strdup(lines[c]);
                    cl[strcspn(cl,"\n")]='\0';
                    if (g_line_nums) printf("%s-%d-%s\n",path,c+1,cl);
                    else             printf("%s-%s\n",path,cl);
                    free(cl);
                }
            }
        }
        free(line_stripped);
    }

    for (int i=0; i<count; i++) free(lines[i]);
    free(lines);
}

static void search_path(const char *path, regex_t *re, const char *raw) {
    struct stat st;
    if (stat(path,&st)!=0) { perror(path); return; }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) { perror(path); return; }
        struct dirent *e;
        while ((e=readdir(dir))) {
            if (e->d_name[0]=='.') continue;
            char sub[4096];
            snprintf(sub,sizeof(sub),"%s/%s",path,e->d_name);
            search_path(sub,re,raw);
        }
        closedir(dir);
    } else {
        search_file(path,re,raw);
    }
}

void rg_print_usage(FILE *out) {
    struct arg_lit *opt_n  = arg_lit0("n",NULL,       "show line numbers");
    struct arg_lit *opt_i  = arg_lit0("i",NULL,       "case-insensitive match");
    struct arg_lit *opt_w  = arg_lit0("w",NULL,       "match whole words");
    struct arg_lit *opt_j  = arg_lit0(NULL,"json",    "output JSON per match");
    struct arg_lit *opt_fs = arg_lit0(NULL,"fixed-strings","treat PATTERN as literal string");
    struct arg_int *opt_C  = arg_int0("C",NULL,"N",   "show N lines of context");
    struct arg_lit *opt_h  = arg_lit0("h","help",     "display this help and exit");
    struct arg_str *opt_p  = arg_str1(NULL,NULL,"PATTERN","regex or literal pattern");
    struct arg_str *opt_f  = arg_strn(NULL,NULL,"PATH",0,100,"file(s) or directories to search");
    struct arg_end *end    = arg_end(10);
    void *argtable[] = { opt_n,opt_i,opt_w,opt_j,opt_fs,opt_C,opt_h,opt_p,opt_f,end };
    fprintf(out,"Usage: rg"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_rg.long_help);
    arg_print_glossary(out,argtable,"  %-26s %s\n");
    arg_free(argtable);
}

int rg_run(int argc, char **argv) {
    struct arg_lit *opt_n  = arg_lit0("n",NULL,       "show line numbers");
    struct arg_lit *opt_i  = arg_lit0("i",NULL,       "case-insensitive match");
    struct arg_lit *opt_w  = arg_lit0("w",NULL,       "match whole words");
    struct arg_lit *opt_j  = arg_lit0(NULL,"json",    "output JSON per match");
    struct arg_lit *opt_fs = arg_lit0(NULL,"fixed-strings","treat PATTERN as literal string");
    struct arg_int *opt_C  = arg_int0("C",NULL,"N",   "show N lines of context");
    struct arg_lit *opt_h  = arg_lit0("h","help",     "display this help and exit");
    struct arg_str *opt_p  = arg_str1(NULL,NULL,"PATTERN","regex or literal pattern");
    struct arg_str *opt_f  = arg_strn(NULL,NULL,"PATH",0,100,"file(s) or directories to search");
    struct arg_end *end    = arg_end(10);
    void *argtable[] = { opt_n,opt_i,opt_w,opt_j,opt_fs,opt_C,opt_h,opt_p,opt_f,end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { rg_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"rg"); arg_free(argtable); return 1; }

    g_json        = opt_j->count>0;
    g_line_nums   = opt_n->count>0;
    g_ignore_case = opt_i->count>0;
    g_whole_word  = opt_w->count>0;
    g_context     = (opt_C->count>0) ? opt_C->ival[0] : 0;
    g_fixed       = opt_fs->count>0;

    const char *raw_pattern = opt_p->sval[0];

    /* Build regex pattern (wrap in \b for whole-word) */
    char pattern_buf[512];
    if (g_whole_word) snprintf(pattern_buf,sizeof(pattern_buf),"\\b%s\\b",raw_pattern);
    else              snprintf(pattern_buf,sizeof(pattern_buf),"%s",raw_pattern);

    regex_t re;
    int rflags = REG_EXTENDED | (g_ignore_case ? REG_ICASE : 0);
    if (!g_fixed && regcomp(&re, pattern_buf, rflags)!=0) {
        fprintf(stderr,"rg: invalid regex: %s\n",raw_pattern);
        arg_free(argtable); return 1;
    }

    /* Default to current directory if no paths given */
    if (opt_f->count==0) {
        search_path(".", g_fixed?NULL:&re, raw_pattern);
    } else {
        for (int i=0; i<opt_f->count; i++)
            search_path(opt_f->sval[i], g_fixed?NULL:&re, raw_pattern);
    }

    if (!g_fixed) regfree(&re);
    arg_free(argtable); return 0;
}

cmd_spec_t spec_rg = {
    .name        = "rg",
    .summary     = "Search text with regex",
    .long_help   = "Recursively search files for PATTERN (regex by default).\n"
                   "Use --fixed-strings for literal matching. Use --json for agent output.",
    .category    = "search",
    .run         = rg_run,
    .print_usage = rg_print_usage,
};
