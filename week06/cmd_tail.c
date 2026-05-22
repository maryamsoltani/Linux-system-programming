#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_tail.h"
#include "json_utils.h"

#define TAIL_MAX_LINES 65536
#define TAIL_LINE_MAX  4096

static int tail_stream(FILE *f, long nlines, long nbytes,
                       int json, char **out_content)
{
    if (nbytes > 0) {
        /* seek from end */
        if (fseek(f, -nbytes, SEEK_END) != 0) {
            fseek(f, 0, SEEK_SET);
        }
        char *buf = NULL;
        size_t buflen = 0, bufcap = 0;
        char tmp[4096];
        size_t got;
        while ((got = fread(tmp, 1, sizeof(tmp), f)) > 0) {
            if (json) {
                size_t need = buflen + got + 1;
                if (need > bufcap) {
                    size_t nc = bufcap == 0 ? 4096 : bufcap * 2;
                    while (nc < need) nc *= 2;
                    char *nb = realloc(buf, nc);
                    if (!nb) { free(buf); return 1; }
                    buf = nb; bufcap = nc;
                }
                memcpy(buf + buflen, tmp, got);
                buflen += got;
                buf[buflen] = '\0';
            } else {
                fwrite(tmp, 1, got, stdout);
            }
        }
        if (json) *out_content = buf;
        return 0;
    }

    /* Ring buffer for lines */
    char **lines = calloc((size_t)nlines, sizeof(char *));
    if (!lines) return 1;

    long head = 0, total = 0;
    char tmp[TAIL_LINE_MAX];

    while (fgets(tmp, sizeof(tmp), f) != NULL) {
        free(lines[head % nlines]);
        lines[head % nlines] = strdup(tmp);
        head++;
        total++;
    }

    long start = total > nlines ? total - nlines : 0;
    char *buf = NULL;
    size_t buflen = 0, bufcap = 0;

    for (long j = start; j < total; j++) {
        const char *line = lines[j % nlines];
        if (!line) continue;
        if (json) {
            size_t len = strlen(line);
            size_t need = buflen + len + 1;
            if (need > bufcap) {
                size_t nc = bufcap == 0 ? 4096 : bufcap * 2;
                while (nc < need) nc *= 2;
                char *nb = realloc(buf, nc);
                if (!nb) {
                    for (int k = 0; k < nlines; k++) free(lines[k]);
                    free(lines); free(buf); return 1;
                }
                buf = nb; bufcap = nc;
            }
            memcpy(buf + buflen, line, len);
            buflen += len;
            buf[buflen] = '\0';
        } else {
            fputs(line, stdout);
        }
    }

    for (long k = 0; k < nlines; k++) free(lines[k]);
    free(lines);

    if (json) *out_content = buf;
    return 0;
}

int tail_run(int argc, char **argv)
{
    long nlines = 10;
    long nbytes = 0;
    int quiet = 0;
    int verbose = 0;
    int follow = 0;
    int json = 0;
    int i;
    int file_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            tail_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1; verbose = 0;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1; quiet = 0;
        } else if (strcmp(argv[i], "-f") == 0) {
            follow = 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "tail: -n requires argument\n"); return 1; }
            nlines = atol(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "tail: -c requires argument\n"); return 1; }
            nbytes = atol(argv[++i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "tail: invalid option: %s\n", argv[i]);
            tail_print_usage(stderr);
            return 1;
        } else {
            file_start = i;
            break;
        }
    }

    int status = 0;
    int nfiles = argc - file_start;

    if (json) printf("{\"command\":\"tail\",\"lines\":%ld,\"files\":[\n", nlines);

    int first = 1;
    if (file_start >= argc) {
        char *content = NULL;
        tail_stream(stdin, nlines, nbytes, json, &content);
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
                fprintf(stderr, "tail: %s: %s\n", argv[i], strerror(errno));
                status = 1;
                continue;
            }
            if (!json) {
                if (verbose || (!quiet && nfiles > 1)) {
                    printf("==> %s <==\n", argv[i]);
                }
            }
            char *content = NULL;
            tail_stream(f, nlines, nbytes, json, &content);

            if (follow && !json) {
                /* simple follow: keep reading */
                char line[4096];
                while (1) {
                    while (fgets(line, sizeof(line), f) != NULL) {
                        fputs(line, stdout);
                        fflush(stdout);
                    }
                    sleep(1);
                }
            }

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

void tail_print_usage(FILE *out)
{
    fprintf(out, "Usage: tail [-n LINES] [-c BYTES] [-q] [-v] [-f] [--json] [FILE...]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Output the last part of files.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-n LINES", "number of lines to print (default: 10)");
    fprintf(out, "  %-20s %s\n", "-c BYTES", "number of bytes to print");
    fprintf(out, "  %-20s %s\n", "-q", "quiet: never print headers");
    fprintf(out, "  %-20s %s\n", "-v", "verbose: always print headers");
    fprintf(out, "  %-20s %s\n", "-f", "follow: output appended data");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_tail_spec = {
    .name        = "tail",
    .summary     = "output the last part of files",
    .long_help   = "Print the last N lines of each file.",
    .run         = tail_run,
    .print_usage = tail_print_usage,
};

void register_tail_command(void)
{
    register_command(&cmd_tail_spec);
}
