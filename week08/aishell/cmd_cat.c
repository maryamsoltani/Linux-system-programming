#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_cat.h"
#include "json_utils.h"

struct cat_options {
    int number_all;
    int number_nonblank;
    int squeeze_blank;
    int show_ends;
    int json;
};

static int cat_file(FILE *in, const char *label, const struct cat_options *opts,
                    char **content_out, size_t *content_len)
{
    char *buf = NULL;
    size_t buflen = 0;
    size_t bufcap = 0;
    char line[4096];
    long lineno = 0;
    long blank_run = 0;

    while (fgets(line, sizeof(line), in) != NULL) {
        size_t len = strlen(line);
        int is_blank = (len == 0 || (len == 1 && line[0] == '\n'));

        if (opts->squeeze_blank) {
            if (is_blank) {
                blank_run++;
                if (blank_run > 1) continue;
            } else {
                blank_run = 0;
            }
        }

        if (opts->json) {
            /* accumulate content */
            size_t need = buflen + len + 1;
            if (need > bufcap) {
                size_t newcap = bufcap == 0 ? 4096 : bufcap * 2;
                while (newcap < need) newcap *= 2;
                char *newbuf = realloc(buf, newcap);
                if (!newbuf) { free(buf); return 1; }
                buf = newbuf;
                bufcap = newcap;
            }
            memcpy(buf + buflen, line, len);
            buflen += len;
            buf[buflen] = '\0';
        } else {
            if (opts->number_all || (opts->number_nonblank && !is_blank)) {
                lineno++;
                printf("%6ld\t", lineno);
            }
            if (opts->show_ends) {
                /* replace trailing newline with $\n */
                if (len > 0 && line[len - 1] == '\n') {
                    line[len - 1] = '\0';
                    printf("%s$\n", line);
                } else {
                    printf("%s", line);
                }
            } else {
                printf("%s", line);
            }
        }
    }

    if (opts->json) {
        *content_out = buf;
        *content_len = buflen;
    }

    (void)label;
    return 0;
}

int cat_run(int argc, char **argv)
{
    struct cat_options opts = {0, 0, 0, 0, 0};
    int i;
    int file_start = argc; /* index of first file arg */
    int status = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            cat_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            opts.json = 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            opts.number_all = 1;
        } else if (strcmp(argv[i], "-b") == 0) {
            opts.number_nonblank = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            opts.squeeze_blank = 1;
        } else if (strcmp(argv[i], "-E") == 0) {
            opts.show_ends = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "cat: invalid option: %s\n", argv[i]);
            cat_print_usage(stderr);
            return 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (opts.json) {
        printf("{\"command\":\"cat\",\"files\":[\n");
        int first = 1;

        if (file_start >= argc) {
            /* stdin */
            char *content = NULL;
            size_t clen = 0;
            cat_file(stdin, "stdin", &opts, &content, &clen);
            if (!first) printf(",\n");
            printf("  {\"path\":\"stdin\",\"content\":");
            json_print_string(stdout, content ? content : "");
            printf("}");
            first = 0;
            free(content);
        } else {
            for (i = file_start; i < argc; i++) {
                FILE *f = fopen(argv[i], "r");
                char *content = NULL;
                size_t clen = 0;
                if (!f) {
                    fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
                    status = 1;
                    continue;
                }
                cat_file(f, argv[i], &opts, &content, &clen);
                fclose(f);
                if (!first) printf(",\n");
                printf("  {\"path\":");
                json_print_string(stdout, argv[i]);
                printf(",\"content\":");
                json_print_string(stdout, content ? content : "");
                printf("}");
                first = 0;
                free(content);
            }
        }
        printf("\n]}\n");
    } else {
        if (file_start >= argc) {
            cat_file(stdin, "stdin", &opts, NULL, NULL);
        } else {
            for (i = file_start; i < argc; i++) {
                FILE *f = fopen(argv[i], "r");
                if (!f) {
                    fprintf(stderr, "cat: %s: %s\n", argv[i], strerror(errno));
                    status = 1;
                    continue;
                }
                cat_file(f, argv[i], &opts, NULL, NULL);
                fclose(f);
            }
        }
    }

    return status;
}

void cat_print_usage(FILE *out)
{
    fprintf(out, "Usage: cat [-n] [-b] [-s] [-E] [--json] [FILE...]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Concatenate and print files (or stdin).\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-n", "number all output lines");
    fprintf(out, "  %-20s %s\n", "-b", "number non-blank lines");
    fprintf(out, "  %-20s %s\n", "-s", "squeeze multiple blank lines");
    fprintf(out, "  %-20s %s\n", "-E", "show $ at end of each line");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_cat_spec = {
    .name        = "cat",
    .summary     = "concatenate and print files",
    .long_help   = "Concatenate and print files to standard output.",
    .run         = cat_run,
    .print_usage = cat_print_usage,
};

void register_cat_command(void)
{
    register_command(&cmd_cat_spec);
}
