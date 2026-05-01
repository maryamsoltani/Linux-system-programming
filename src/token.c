/*
 * token.c — tokenizer.
 *
 * Splits a line into tokens.  Whitespace separates tokens; the shell
 * metacharacters (| & ; < >) are emitted as standalone tokens whether
 * or not they have surrounding whitespace, so `echo hi>out.txt` and
 * `echo hi > out.txt` produce the same token stream.
 *
 * The line buffer is mutated: word boundaries get NUL terminators
 * spliced in.  Metacharacter tokens are returned as pointers into a
 * private static table so they don't need to fit in `line` at all.
 *
 * The caller owns `line` and `tokens`; this function owns neither.
 * `line` must outlive the returned tokens because word tokens point
 * into it.
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "token.h"

/* Static one-character strings for each metacharacter token. */
static char  META_STR[5][2] = { "|", "&", ";", "<", ">" };
static const char META_CHARS[] = "|&;<>";

/* Static two-character string for the `>>` operator. */
static char APPEND_STR[] = ">>";

static int is_meta(int c)
{
    return c != '\0' && strchr(META_CHARS, c) != NULL;
}

static char *meta_token(char c)
{
    const char *hit = strchr(META_CHARS, c);
    return META_STR[hit - META_CHARS];
}

int tokenize(char *line, char **tokens)
{
    int   n = 0;
    char *p = line;

    while (*p != '\0') {
        /* Skip whitespace between tokens */
        while (*p != '\0' && isspace((unsigned char)*p))
            p++;
        if (*p == '\0')
            break;

        if (n >= MAX_TOKENS - 1)
            return -1;

        if (is_meta((unsigned char)*p)) {
            if (*p == '>' && *(p + 1) == '>') {
                tokens[n++] = APPEND_STR;
                p += 2;
            } else {
                tokens[n++] = meta_token(*p);
                p++;
            }
            continue;
        }

        /* Regular word: run until whitespace or metacharacter */
        tokens[n++] = p;
        while (*p != '\0' &&
               !isspace((unsigned char)*p) &&
               !is_meta((unsigned char)*p))
            p++;

        if (*p == '\0')
            break;

        if (is_meta((unsigned char)*p)) {
            /* The word ends here at a metacharacter.  Save the metachar,
             * NUL-terminate the word in place, then emit the metachar
             * token from the static table and advance past it. */
            char saved = *p;
            *p = '\0';
            if (n >= MAX_TOKENS - 1)
                return -1;
            if (saved == '>' && *(p + 1) == '>') {
                tokens[n++] = APPEND_STR;
                p += 2;
            } else {
                tokens[n++] = meta_token(saved);
                p++;
            }
        } else {
            /* Whitespace ended the word */
            *p++ = '\0';
        }
    }

    tokens[n] = NULL;
    return n;
}
