#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd_spec.h"
#include "cmd_echo.h"
#include "json_utils.h"

static void echo_print_escaped(const char *s)
{
    while (*s) {
        if (*s == '\\' && *(s + 1)) {
            s++;
            switch (*s) {
            case 'n':  putchar('\n'); break;
            case 't':  putchar('\t'); break;
            case 'r':  putchar('\r'); break;
            case '\\': putchar('\\'); break;
            case 'a':  putchar('\a'); break;
            case 'b':  putchar('\b'); break;
            case 'f':  putchar('\f'); break;
            case 'v':  putchar('\v'); break;
            case '0':  putchar('\0'); break;
            default:   putchar('\\'); putchar(*s); break;
            }
        } else {
            putchar(*s);
        }
        s++;
    }
}

int echo_run(int argc, char **argv)
{
    int no_newline = 0;
    int interpret_escapes = 0;
    int json = 0;
    int i;
    int text_start = argc;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            echo_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            no_newline = 1;
        } else if (strcmp(argv[i], "-e") == 0) {
            interpret_escapes = 1;
        } else if (strcmp(argv[i], "-E") == 0) {
            interpret_escapes = 0;
        } else {
            text_start = i;
            break;
        }
    }

    /* Build output text */
    char text_buf[65536];
    size_t pos = 0;
    text_buf[0] = '\0';

    for (i = text_start; i < argc; i++) {
        if (i > text_start) {
            if (pos < sizeof(text_buf) - 1) text_buf[pos++] = ' ';
        }
        size_t len = strlen(argv[i]);
        if (pos + len < sizeof(text_buf) - 1) {
            memcpy(text_buf + pos, argv[i], len);
            pos += len;
        }
    }
    text_buf[pos] = '\0';

    if (json) {
        printf("{\"command\":\"echo\",\"text\":");
        json_print_string(stdout, text_buf);
        printf(",\"trailing_newline\":%s,\"interpret_escapes\":%s}\n",
               no_newline ? "false" : "true",
               interpret_escapes ? "true" : "false");
        return 0;
    }

    if (interpret_escapes) {
        echo_print_escaped(text_buf);
    } else {
        fputs(text_buf, stdout);
    }
    if (!no_newline) putchar('\n');

    return 0;
}

void echo_print_usage(FILE *out)
{
    fprintf(out, "Usage: echo [-n] [-e] [-E] [--json] [TEXT...]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  Print text to standard output.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-n", "do not output trailing newline");
    fprintf(out, "  %-20s %s\n", "-e", "interpret backslash escapes");
    fprintf(out, "  %-20s %s\n", "-E", "disable backslash escapes (default)");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_echo_spec = {
    .name        = "echo",
    .summary     = "print text to stdout",
    .long_help   = "Print text to standard output.",
    .run         = echo_run,
    .print_usage = echo_print_usage,
};

void register_echo_command(void)
{
    register_command(&cmd_echo_spec);
}
