#include <stdio.h>
#include <stdlib.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

void cat_print_usage(FILE *out) {
    struct arg_lit *opt_n = arg_lit0("n","number","number all output lines");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {path, content}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to print");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, opt_f, end };
    fprintf(out, "Usage: cat");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_cat.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

int cat_run(int argc, char **argv) {
    struct arg_lit *opt_n = arg_lit0("n","number","number all output lines");
    struct arg_lit *opt_j = arg_lit0(NULL,"json","output JSON {path, content}");
    struct arg_lit *opt_h = arg_lit0("h","help","display this help and exit");
    struct arg_str *opt_f = arg_strn(NULL,NULL,"FILE",1,100,"file(s) to print");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, opt_f, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (opt_h->count > 0) { cat_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) { arg_print_errors(stderr,end,"cat"); arg_free(argtable); return 1; }

    int number = opt_n->count > 0;
    int json   = opt_j->count > 0;
    int ret    = 0;

    for (int i = 0; i < opt_f->count; i++) {
        const char *fname = opt_f->sval[i];
        FILE *fp = fopen(fname, "r");
        if (!fp) {
            fprintf(stderr, "cat: %s: No such file or directory\n", fname);
            ret = 1; continue;
        }

        if (json) {
            /* Read entire file into buffer */
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            rewind(fp);
            char *buf = malloc(sz + 1);
            if (!buf) { fclose(fp); ret = 1; continue; }
            fread(buf, 1, sz, fp);
            buf[sz] = '\0';

            /* JSON-escape the content */
            printf("{\"path\":\"%s\",\"content\":\"", fname);
            for (char *p = buf; *p; p++) {
                if (*p == '"')       printf("\\\"");
                else if (*p == '\\') printf("\\\\");
                else if (*p == '\n') printf("\\n");
                else if (*p == '\r') printf("\\r");
                else if (*p == '\t') printf("\\t");
                else                 putchar(*p);
            }
            printf("\"}\n");
            free(buf);
        } else {
            char line[4096];
            long lineno = 1;
            while (fgets(line, sizeof(line), fp)) {
                if (number) printf("%6ld\t%s", lineno++, line);
                else        fputs(line, stdout);
            }
        }
        fclose(fp);
    }
    arg_free(argtable);
    return ret;
}

cmd_spec_t spec_cat = {
    .name        = "cat",
    .summary     = "Concatenate and print files",
    .long_help   = "Print file contents to stdout. Use -n to number lines.\n"
                   "Use --json to wrap output as {\"path\":...,\"content\":...}.",
    .category    = "filesystem",
    .run         = cat_run,
    .print_usage = cat_print_usage,
};
