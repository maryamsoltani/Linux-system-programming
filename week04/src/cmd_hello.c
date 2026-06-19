#include <stdio.h>
#include "../argtable3/argtable3.h"
#include "cmd_spec.h"

void hello_print_usage(FILE *out) {
    struct arg_str *name  = arg_str0("n", "name", "NAME", "name to greet (default: World)");
    struct arg_lit *opt_j = arg_lit0(NULL, "json", "output JSON {message: \"...\"}");
    struct arg_lit *opt_h = arg_lit0("h",  "help", "display this help and exit");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { name, opt_j, opt_h, end };
    fprintf(out, "Usage: hello");
    arg_print_syntax(out, argtable, "\n");
    fprintf(out, "\n%s\n\nOptions:\n", spec_hello.long_help);
    arg_print_glossary(out, argtable, "  %-22s %s\n");
    arg_free(argtable);
}

int hello_run(int argc, char **argv) {
    struct arg_str *name  = arg_str0("n", "name", "NAME", "name to greet (default: World)");
    struct arg_lit *opt_j = arg_lit0(NULL, "json", "output JSON {message: \"...\"}");
    struct arg_lit *opt_h = arg_lit0("h",  "help", "display this help and exit");
    struct arg_end *end   = arg_end(10);
    void *argtable[] = { name, opt_j, opt_h, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (opt_h->count > 0) { hello_print_usage(stdout); arg_free(argtable); return 0; }
    if (nerrors > 0) { arg_print_errors(stderr, end, "hello"); arg_free(argtable); return 1; }

    const char *who = name->count > 0 ? name->sval[0] : "World";

    if (opt_j->count > 0)
        printf("{\"message\":\"Hello, %s!\"}\n", who);
    else
        printf("Hello, %s!\n", who);

    arg_free(argtable);
    return 0;
}

cmd_spec_t spec_hello = {
    .name        = "hello",
    .version     = "3.0.0",
    .summary     = "Print a greeting message",
    .long_help   = "Print \"Hello, World!\" or greet a specific name.\n"
                   "Use --json to wrap output as {\"message\":\"...\"}.",
    .category    = "shell",
    .run         = hello_run,
    .print_usage = hello_print_usage,
};
