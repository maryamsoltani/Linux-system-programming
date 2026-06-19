#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

/* ════════════════════════════════════════════════════════════════════════════
 * Shared: load file into lines array
 * ════════════════════════════════════════════════════════════════════════════ */

static char **load_lines(const char *path, int *count) {
    FILE *fp = fopen(path,"r");
    if (!fp) { perror(path); return NULL; }
    char **lines = NULL; int cap=0; *count=0;
    char buf[4096];
    while (fgets(buf,sizeof(buf),fp)) {
        if (*count>=cap) { cap = cap?cap*2:64; lines=realloc(lines,cap*sizeof(char*)); }
        lines[(*count)++] = strdup(buf);
    }
    fclose(fp); return lines;
}

static int save_lines(const char *path, char **lines, int count) {
    FILE *fp = fopen(path,"w");
    if (!fp) { perror(path); return 1; }
    for (int i=0;i<count;i++) fputs(lines[i],fp);
    fclose(fp); return 0;
}

static void free_lines(char **lines, int count) {
    for (int i=0;i<count;i++) free(lines[i]);
    free(lines);
}

/* ════════════════════════════════════════════════════════════════════════════
 * edit-replace-line   FILE N TEXT
 * ════════════════════════════════════════════════════════════════════════════ */

void edit_replace_line_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_int *opt_n = arg_int1(NULL,NULL,"N","line number to replace (1-based)");
    struct arg_str *opt_t = arg_str1(NULL,NULL,"TEXT","replacement text");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, opt_n, opt_t, end };
    fprintf(out,"Usage: edit-replace-line"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_edit_replace_line.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int edit_replace_line_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_int *opt_n = arg_int1(NULL,NULL,"N","line number to replace (1-based)");
    struct arg_str *opt_t = arg_str1(NULL,NULL,"TEXT","replacement text");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, opt_n, opt_t, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { edit_replace_line_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"edit-replace-line"); arg_free(argtable); return 1; }

    const char *path = opt_f->sval[0];
    int lineno       = opt_n->ival[0];
    const char *text = opt_t->sval[0];
    int json         = opt_j->count>0;

    int count; char **lines = load_lines(path,&count);
    if (!lines) { arg_free(argtable); return 1; }

    if (lineno<1 || lineno>count) {
        fprintf(stderr,"edit-replace-line: line %d out of range (file has %d lines)\n",lineno,count);
        free_lines(lines,count); arg_free(argtable); return 1;
    }

    free(lines[lineno-1]);
    char newline[4096]; snprintf(newline,sizeof(newline),"%s\n",text);
    lines[lineno-1] = strdup(newline);

    int r = save_lines(path,lines,count);
    if (json) printf("{\"path\":\"%s\",\"line\":%d,\"status\":\"%s\"}\n",
                     path,lineno,r==0?"ok":"error");
    free_lines(lines,count); arg_free(argtable); return r;
}

cmd_spec_t spec_edit_replace_line = {
    .name="edit-replace-line", .summary="Replace a single line in a file",
    .long_help="Replace line N (1-based) in FILE with TEXT.",
    .category="edit", .run=edit_replace_line_run, .print_usage=edit_replace_line_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * edit-insert-line   FILE N TEXT
 * ════════════════════════════════════════════════════════════════════════════ */

void edit_insert_line_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_int *opt_n = arg_int1(NULL,NULL,"N","insert before this line number (1-based)");
    struct arg_str *opt_t = arg_str1(NULL,NULL,"TEXT","text to insert");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, opt_n, opt_t, end };
    fprintf(out,"Usage: edit-insert-line"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_edit_insert_line.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int edit_insert_line_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_int *opt_n = arg_int1(NULL,NULL,"N","insert before this line number (1-based)");
    struct arg_str *opt_t = arg_str1(NULL,NULL,"TEXT","text to insert");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, opt_n, opt_t, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { edit_insert_line_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"edit-insert-line"); arg_free(argtable); return 1; }

    const char *path = opt_f->sval[0];
    int lineno       = opt_n->ival[0];
    const char *text = opt_t->sval[0];
    int json         = opt_j->count>0;

    int count; char **lines = load_lines(path,&count);
    if (!lines) { arg_free(argtable); return 1; }

    if (lineno<1 || lineno>count+1) {
        fprintf(stderr,"edit-insert-line: line %d out of range\n",lineno);
        free_lines(lines,count); arg_free(argtable); return 1;
    }

    /* Grow array by 1 */
    char **newlines = malloc((count+1)*sizeof(char*));
    for (int i=0; i<lineno-1; i++)  newlines[i]        = lines[i];
    char newline[4096]; snprintf(newline,sizeof(newline),"%s\n",text);
    newlines[lineno-1] = strdup(newline);
    for (int i=lineno-1; i<count; i++) newlines[i+1]   = lines[i];
    free(lines);

    int r = save_lines(path,newlines,count+1);
    if (json) printf("{\"path\":\"%s\",\"line\":%d,\"status\":\"%s\"}\n",
                     path,lineno,r==0?"ok":"error");
    free_lines(newlines,count+1); arg_free(argtable); return r;
}

cmd_spec_t spec_edit_insert_line = {
    .name="edit-insert-line", .summary="Insert a line before a given line number",
    .long_help="Insert TEXT as a new line before line N in FILE.",
    .category="edit", .run=edit_insert_line_run, .print_usage=edit_insert_line_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * edit-delete-line   FILE N
 * ════════════════════════════════════════════════════════════════════════════ */

void edit_delete_line_print_usage(FILE *out) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_int *opt_n = arg_int1(NULL,NULL,"N","line number to delete (1-based)");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, opt_n, end };
    fprintf(out,"Usage: edit-delete-line"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_edit_delete_line.long_help);
    arg_print_glossary(out,argtable,"  %-22s %s\n");
    arg_free(argtable);
}

int edit_delete_line_run(int argc, char **argv) {
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON status");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_int *opt_n = arg_int1(NULL,NULL,"N","line number to delete (1-based)");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_j, opt_h, opt_f, opt_n, end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { edit_delete_line_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"edit-delete-line"); arg_free(argtable); return 1; }

    const char *path = opt_f->sval[0];
    int lineno       = opt_n->ival[0];
    int json         = opt_j->count>0;

    int count; char **lines = load_lines(path,&count);
    if (!lines) { arg_free(argtable); return 1; }

    if (lineno<1 || lineno>count) {
        fprintf(stderr,"edit-delete-line: line %d out of range\n",lineno);
        free_lines(lines,count); arg_free(argtable); return 1;
    }

    free(lines[lineno-1]);
    for (int i=lineno-1; i<count-1; i++) lines[i]=lines[i+1];
    int r = save_lines(path,lines,count-1);
    if (json) printf("{\"path\":\"%s\",\"line\":%d,\"status\":\"%s\"}\n",
                     path,lineno,r==0?"ok":"error");
    free(lines); arg_free(argtable); return r;
}

cmd_spec_t spec_edit_delete_line = {
    .name="edit-delete-line", .summary="Delete a single line from a file",
    .long_help="Delete line N (1-based) from FILE.",
    .category="edit", .run=edit_delete_line_run, .print_usage=edit_delete_line_print_usage,
};

/* ════════════════════════════════════════════════════════════════════════════
 * edit-replace   FILE PATTERN REPLACEMENT
 * ════════════════════════════════════════════════════════════════════════════ */

void edit_replace_print_usage(FILE *out) {
    struct arg_lit *opt_i  = arg_lit0("i",NULL,"case-insensitive regex");
    struct arg_lit *opt_fs = arg_lit0(NULL,"fixed-strings","treat PATTERN as literal");
    struct arg_lit *opt_j  = arg_lit0(NULL,"json","output JSON {matches, replacements}");
    struct arg_lit *opt_h  = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f  = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_str *opt_p  = arg_str1(NULL,NULL,"PATTERN","regex or literal to find");
    struct arg_str *opt_r  = arg_str1(NULL,NULL,"REPLACEMENT","replacement string");
    struct arg_end *end    = arg_end(10);
    void *argtable[] = { opt_i,opt_fs,opt_j,opt_h,opt_f,opt_p,opt_r,end };
    fprintf(out,"Usage: edit-replace"); arg_print_syntax(out,argtable,"\n");
    fprintf(out,"\n%s\n\nOptions:\n",spec_edit_replace.long_help);
    arg_print_glossary(out,argtable,"  %-26s %s\n");
    arg_free(argtable);
}

int edit_replace_run(int argc, char **argv) {
    struct arg_lit *opt_i  = arg_lit0("i",NULL,"case-insensitive regex");
    struct arg_lit *opt_fs = arg_lit0(NULL,"fixed-strings","treat PATTERN as literal");
    struct arg_lit *opt_j  = arg_lit0(NULL,"json","output JSON {matches, replacements}");
    struct arg_lit *opt_h  = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f  = arg_str1(NULL,NULL,"FILE","file to edit");
    struct arg_str *opt_p  = arg_str1(NULL,NULL,"PATTERN","regex or literal to find");
    struct arg_str *opt_r  = arg_str1(NULL,NULL,"REPLACEMENT","replacement string");
    struct arg_end *end    = arg_end(10);
    void *argtable[] = { opt_i,opt_fs,opt_j,opt_h,opt_f,opt_p,opt_r,end };

    int nerrors = arg_parse(argc,argv,argtable);
    if (opt_h->count>0) { edit_replace_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors>0) { arg_print_errors(stderr,end,"edit-replace"); arg_free(argtable); return 1; }

    const char *path    = opt_f->sval[0];
    const char *pattern = opt_p->sval[0];
    const char *repl    = opt_r->sval[0];
    int json   = opt_j->count>0;
    int fixed  = opt_fs->count>0;
    int icase  = opt_i->count>0;

    int count; char **lines = load_lines(path,&count);
    if (!lines) { arg_free(argtable); return 1; }

    regex_t re;
    if (!fixed) {
        int rflags = REG_EXTENDED | (icase ? REG_ICASE : 0);
        if (regcomp(&re,pattern,rflags)!=0) {
            fprintf(stderr,"edit-replace: invalid regex: %s\n",pattern);
            free_lines(lines,count); arg_free(argtable); return 1;
        }
    }

    int matches=0, replacements=0;
    for (int i=0; i<count; i++) {
        char result[8192]; result[0]='\0';
        char *src = lines[i];

        if (fixed) {
            /* Simple literal replace */
            char *p;
            while ((p=strstr(src,pattern))!=NULL) {
                matches++;
                strncat(result,src,p-src);
                strcat(result,repl);
                src = p + strlen(pattern);
                replacements++;
            }
            strcat(result,src);
        } else {
            regmatch_t m;
            while (regexec(&re,src,1,&m,0)==0) {
                matches++;
                strncat(result,src,m.rm_so);
                strcat(result,repl);
                src += m.rm_eo;
                replacements++;
                if (m.rm_eo==m.rm_so) { strncat(result,src,1); src++; } /* avoid infinite loop */
            }
            strcat(result,src);
        }

        free(lines[i]);
        lines[i] = strdup(result);
    }

    if (!fixed) regfree(&re);

    int r = save_lines(path,lines,count);
    if (json) printf("{\"path\":\"%s\",\"matches\":%d,\"replacements\":%d,\"status\":\"%s\"}\n",
                     path,matches,replacements,r==0?"ok":"error");
    else if (!r) printf("edit-replace: %d replacement(s) in %s\n",replacements,path);

    free_lines(lines,count); arg_free(argtable); return r;
}

cmd_spec_t spec_edit_replace = {
    .name="edit-replace", .summary="Global find/replace with regex in a file",
    .long_help="Replace all occurrences of PATTERN with REPLACEMENT in FILE.\n"
               "Supports regex (default) or literal (--fixed-strings) matching.",
    .category="edit", .run=edit_replace_run, .print_usage=edit_replace_print_usage,
};
