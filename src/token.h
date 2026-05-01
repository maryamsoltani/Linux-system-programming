/*
 * token.h — input tokenizer interface.
 *
 * tokenize() splits a writable input line into a NULL-terminated array
 * of token pointers that point INTO the original buffer (no copying).
 * The caller owns the line buffer and the token array; tokenize() owns
 * neither.
 *
 * Tokens are split on whitespace, with the shell metacharacters
 *   |  &  ;  <  >
 * always emitted as their own tokens regardless of surrounding spacing,
 * so `echo hi>out.txt` and `echo hi > out.txt` produce identical tokens.
 *
 * Returns the number of tokens produced.  On overflow returns -1.
 */

#ifndef TOKEN_H
#define TOKEN_H

#define MAX_TOKENS 1024

int tokenize(char *line, char **tokens);

#endif /* TOKEN_H */
