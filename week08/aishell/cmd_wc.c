#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_wc.h"
#include "json_utils.h"

struct wc_counts {
    long lines;
    long words;
    long bytes;
};

static void count_stream(FILE *f, struct wc_counts *c)
{
    int ch;
    int in_word = 0;
    c->lines = 0; c->words = 0; c->bytes = 0;
    while ((ch = fgetc(f)) != EOF) {
        c->bytes++;
        if (ch == '\n') c->lines++;
        if (isspace(ch)) {
            in_word = 0;
        } else {
            if (!in_word) { c->words++; in_word = 1; }
        }
    }
}

int wc_run(int argc, char **argv)
{
    int opt_lines = 0, opt_words = 0, opt_bytes = 0, json = 0;
    int i;
    int file_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            wc_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-l") == 0) {
            opt_lines = 1;
        } else if (strcmp(argv[i], "-w") == 0) {
            opt_words = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            opt_bytes = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "wc: invalid option: %s\n", argv[i]);
            wc_print_usage(stderr);
            return 1;
        } else {
            file_start = i;
            break;
        }
    }

    /* If none specified, show all */
    int show_all = (!opt_lines && !opt_words && !opt_bytes);

    int status = 0;

    if (json) printf("{\"files\":[\n");

    int first = 1;
    if (file_start >= argc) {
        /* stdin */
        struct wc_counts c;
        count_stream(stdin, &c);
        if (json) {
            printf("  {\"lines\":%ld,\"words\":%ld,\"bytes\":%ld,\"path\":\"stdin\"}",
                   c.lines, c.words, c.bytes);
            first = 0;
        } else {
            if (show_all || opt_lines)  printf("%7ld ", c.lines);
            if (show_all || opt_words)  printf("%7ld ", c.words);
            if (show_all || opt_bytes)  printf("%7ld ", c.bytes);
            printf("stdin\n");
        }
    } else {
        for (i = file_start; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "wc: %s: %s\n", argv[i], strerror(errno));
                status = 1;
                continue;
            }
            struct wc_counts c;
            count_stream(f, &c);
            fclose(f);
            if (json) {
                if (!first) printf(",\n");
                printf("  {\"lines\":%ld,\"words\":%ld,\"bytes\":%ld,\"path\":",
                       c.lines, c.words, c.bytes);
                json_print_string(stdout, argv[i]);
                printf("}");
                first = 0;
            } else {
                if (show_all || opt_lines)  printf("%7ld ", c.lines);
                if (show_all || opt_words)  printf("%7ld ", c.words);
                if (show_all || opt_bytes)  printf("%7ld ", c.bytes);
                printf("%s\n", argv[i]);
            }
        }
    }

    if (json) printf("\n]}\n");

    return status;
}

void wc_print_usage(FILE *out)
{
    fprintf(out, "Usage: wc [-l] [-w] [-c] [--json] [FILE...]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Count lines, words, and bytes in files (or stdin).\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-l", "count lines only");
    fprintf(out, "  %-20s %s\n", "-w", "count words only");
    fprintf(out, "  %-20s %s\n", "-c", "count bytes only");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_wc_spec = {
    .name        = "wc",
    .summary     = "count lines, words, and bytes",
    .long_help   = "Count lines, words, and bytes in files or stdin.",
    .run         = wc_run,
    .print_usage = wc_print_usage,
};

void register_wc_command(void)
{
    register_command(&cmd_wc_spec);
}
