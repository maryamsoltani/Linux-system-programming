#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

/* ─── head ─── */

void head_print_usage(FILE *out) {
    struct arg_int *opt_n = arg_int0("n","lines","N","number of lines (default: 10)");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {path, lines:[...]}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to read");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, opt_f, end };
    fprintf(out, "Usage: head");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_head.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

int head_run(int argc, char **argv) {
    struct arg_int *opt_n = arg_int0("n","lines","N","number of lines (default: 10)");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {path, lines:[...]}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to read");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, opt_f, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (opt_h->count > 0) { head_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) { arg_print_errors(stderr,end,"head"); arg_free(argtable); return 1; }

    int nlines = (opt_n->count > 0) ? opt_n->ival[0] : 10;
    int json   = opt_j->count > 0;
    int ret    = 0;

    for (int i = 0; i < opt_f->count; i++) {
        const char *fname = opt_f->sval[i];
        FILE *fp = fopen(fname, "r");
        if (!fp) { fprintf(stderr,"head: %s: No such file\n",fname); ret=1; continue; }

        char line[4096];
        int count = 0;

        if (json) {
            printf("{\"path\":\"%s\",\"lines\":[", fname);
            int first = 1;
            while (count < nlines && fgets(line, sizeof(line), fp)) {
                /* strip trailing newline for JSON */
                line[strcspn(line, "\n")] = '\0';
                if (!first) printf(",");
                printf("\"%s\"", line);
                first = 0; count++;
            }
            printf("]}\n");
        } else {
            while (count < nlines && fgets(line, sizeof(line), fp))
                { fputs(line, stdout); count++; }
        }
        fclose(fp);
    }
    arg_free(argtable);
    return ret;
}

cmd_spec_t spec_head = {
    .name        = "head",
    .summary     = "Print first N lines of a file",
    .long_help   = "Output the first N lines of each FILE (default 10).\n"
                   "Use --json for {\"path\":...,\"lines\":[...]} output.",
    .category    = "filesystem",
    .run         = head_run,
    .print_usage = head_print_usage,
};

/* ─── tail ─── */

void tail_print_usage(FILE *out) {
    struct arg_int *opt_n = arg_int0("n","lines","N","number of lines (default: 10)");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {path, lines:[...]}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to read");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, opt_f, end };
    fprintf(out, "Usage: tail");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_tail.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

int tail_run(int argc, char **argv) {
    struct arg_int *opt_n = arg_int0("n","lines","N","number of lines (default: 10)");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {path, lines:[...]}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to read");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, opt_f, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (opt_h->count > 0) { tail_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) { arg_print_errors(stderr,end,"tail"); arg_free(argtable); return 1; }

    int nlines = (opt_n->count > 0) ? opt_n->ival[0] : 10;
    int json   = opt_j->count > 0;
    int ret    = 0;

    char **ring = malloc(nlines * sizeof(char *));
    if (!ring) { perror("tail"); arg_free(argtable); return 1; }
    for (int i = 0; i < nlines; i++) ring[i] = NULL;

    for (int i = 0; i < opt_f->count; i++) {
        const char *fname = opt_f->sval[i];
        FILE *fp = fopen(fname, "r");
        if (!fp) { fprintf(stderr,"tail: %s: No such file\n",fname); ret=1; continue; }

        for (int j = 0; j < nlines; j++) { free(ring[j]); ring[j] = NULL; }

        char line[4096];
        int idx = 0, total = 0;
        while (fgets(line, sizeof(line), fp)) {
            free(ring[idx]);
            ring[idx] = strdup(line);
            idx = (idx + 1) % nlines;
            total++;
        }
        fclose(fp);

        int start = (total < nlines) ? 0 : idx;
        int count = (total < nlines) ? total : nlines;

        if (json) {
            printf("{\"path\":\"%s\",\"lines\":[", fname);
            for (int j = 0; j < count; j++) {
                int pos = (start + j) % nlines;
                char *l = ring[pos] ? ring[pos] : "";
                l[strcspn(l, "\n")] = '\0';
                if (j > 0) printf(",");
                printf("\"%s\"", l);
            }
            printf("]}\n");
        } else {
            for (int j = 0; j < count; j++) {
                int pos = (start + j) % nlines;
                if (ring[pos]) fputs(ring[pos], stdout);
            }
        }
    }

    for (int i = 0; i < nlines; i++) free(ring[i]);
    free(ring);
    arg_free(argtable);
    return ret;
}

cmd_spec_t spec_tail = {
    .name        = "tail",
    .summary     = "Print last N lines of a file",
    .long_help   = "Output the last N lines of each FILE (default 10).\n"
                   "Use --json for {\"path\":...,\"lines\":[...]} output.",
    .category    = "filesystem",
    .run         = tail_run,
    .print_usage = tail_print_usage,
};
