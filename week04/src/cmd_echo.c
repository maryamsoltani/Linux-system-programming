#include <stdio.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

void echo_print_usage(FILE *out) {
    struct arg_lit *opt_n = arg_lit0("n", NULL,     "do not output a trailing newline");
    struct arg_lit *opt_j = arg_lit0(NULL, "json",  "output JSON {text: \"...\"}");
    struct arg_lit *opt_h = arg_lit0("h", "help",   "display this help and exit");
    struct arg_str *words = arg_strn(NULL, NULL, "STRING", 0, 100, "text to print");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, words, end };
    fprintf(out, "Usage: echo");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_echo.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

int echo_run(int argc, char **argv) {
    struct arg_lit *opt_n = arg_lit0("n", NULL,     "do not output a trailing newline");
    struct arg_lit *opt_j = arg_lit0(NULL, "json",  "output JSON {text: \"...\"}");
    struct arg_lit *opt_h = arg_lit0("h", "help",   "display this help and exit");
    struct arg_str *words = arg_strn(NULL, NULL, "STRING", 0, 100, "text to print");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { opt_n, opt_j, opt_h, words, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (opt_h->count > 0) { echo_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) { arg_print_errors(stderr, end, "echo"); arg_free(argtable); return 1; }

    if (opt_j->count > 0) {
        printf("{\"text\":\"");
        for (int i = 0; i < words->count; i++) {
            if (i > 0) putchar(' ');
            for (const char *p = words->sval[i]; *p; p++) {
                if (*p == '"')       printf("\\\"");
                else if (*p == '\\') printf("\\\\");
                else                 putchar(*p);
            }
        }
        printf("\"}\n");
    } else {
        for (int i = 0; i < words->count; i++) {
            if (i > 0) putchar(' ');
            fputs(words->sval[i], stdout);
        }
        if (opt_n->count == 0) putchar('\n');
    }

    arg_free(argtable);
    return 0;
}

cmd_spec_t spec_echo = {
    .name        = "echo",
    .version     = "1.0.0",
    .summary     = "Print text to standard output",
    .long_help   = "Write each STRING to stdout, separated by spaces.\n"
                   "Use -n to suppress the trailing newline.\n"
                   "Use --json to wrap output as {\"text\":\"...\"}.",
    .category    = "shell",
    .run         = echo_run,
    .print_usage = echo_print_usage,
};
