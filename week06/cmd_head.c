#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_head.h"
#include "json_utils.h"

static int head_stream(FILE *f, long nlines, long nbytes,
                       int json, char **out_content)
{
    char line[4096];
    long count = 0;
    char *buf = NULL;
    size_t buflen = 0, bufcap = 0;

    if (nbytes > 0) {
        /* byte mode */
        long remaining = nbytes;
        while (remaining > 0) {
            size_t toread = (size_t)(remaining < (long)sizeof(line) ? remaining : (long)sizeof(line));
            size_t got = fread(line, 1, toread, f);
            if (got == 0) break;
            if (json) {
                size_t need = buflen + got + 1;
                if (need > bufcap) {
                    size_t nc = bufcap == 0 ? 4096 : bufcap * 2;
                    while (nc < need) nc *= 2;
                    char *nb = realloc(buf, nc);
                    if (!nb) { free(buf); return 1; }
                    buf = nb; bufcap = nc;
                }
                memcpy(buf + buflen, line, got);
                buflen += got;
                buf[buflen] = '\0';
            } else {
                fwrite(line, 1, got, stdout);
            }
            remaining -= (long)got;
        }
    } else {
        while (fgets(line, sizeof(line), f) != NULL) {
            if (count >= nlines) break;
            if (json) {
                size_t len = strlen(line);
                size_t need = buflen + len + 1;
                if (need > bufcap) {
                    size_t nc = bufcap == 0 ? 4096 : bufcap * 2;
                    while (nc < need) nc *= 2;
                    char *nb = realloc(buf, nc);
                    if (!nb) { free(buf); return 1; }
                    buf = nb; bufcap = nc;
                }
                memcpy(buf + buflen, line, len);
                buflen += len;
                buf[buflen] = '\0';
            } else {
                fputs(line, stdout);
            }
            count++;
        }
    }

    if (json) {
        *out_content = buf;
    }
    return 0;
}

int head_run(int argc, char **argv)
{
    long nlines = 10;
    long nbytes = 0;
    int quiet = 0;
    int verbose = 0;
    int json = 0;
    int i;
    int file_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            head_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1; verbose = 0;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1; quiet = 0;
        } else if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "head: -n requires argument\n"); return 1; }
            nlines = atol(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "head: -c requires argument\n"); return 1; }
            nbytes = atol(argv[++i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "head: invalid option: %s\n", argv[i]);
            head_print_usage(stderr);
            return 1;
        } else {
            file_start = i;
            break;
        }
    }

    int status = 0;
    int nfiles = argc - file_start;

    if (json) printf("{\"command\":\"head\",\"lines\":%ld,\"files\":[\n", nlines);

    int first = 1;
    if (file_start >= argc) {
        /* stdin */
        char *content = NULL;
        head_stream(stdin, nlines, nbytes, json, &content);
        if (json) {
            printf("  {\"path\":\"stdin\",\"content\":");
            json_print_string(stdout, content ? content : "");
            printf("}");
            first = 0;
        }
        free(content);
    } else {
        for (i = file_start; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "head: %s: %s\n", argv[i], strerror(errno));
                status = 1;
                continue;
            }
            if (!json) {
                if (verbose || (!quiet && nfiles > 1)) {
                    printf("==> %s <==\n", argv[i]);
                }
            }
            char *content = NULL;
            head_stream(f, nlines, nbytes, json, &content);
            fclose(f);
            if (json) {
                if (!first) printf(",\n");
                printf("  {\"path\":");
                json_print_string(stdout, argv[i]);
                printf(",\"content\":");
                json_print_string(stdout, content ? content : "");
                printf("}");
                first = 0;
            }
            free(content);
            if (!json && i + 1 < argc && !quiet) printf("\n");
        }
    }

    if (json) printf("\n]}\n");

    return status;
}

void head_print_usage(FILE *out)
{
    fprintf(out, "Usage: head [-n LINES] [-c BYTES] [-q] [-v] [--json] [FILE...]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Output the first part of files.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-n LINES", "number of lines to print (default: 10)");
    fprintf(out, "  %-20s %s\n", "-c BYTES", "number of bytes to print");
    fprintf(out, "  %-20s %s\n", "-q", "quiet: never print headers");
    fprintf(out, "  %-20s %s\n", "-v", "verbose: always print headers");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_head_spec = {
    .name        = "head",
    .summary     = "output the first part of files",
    .long_help   = "Print the first N lines of each file.",
    .run         = head_run,
    .print_usage = head_print_usage,
};

void register_head_command(void)
{
    register_command(&cmd_head_spec);
}
