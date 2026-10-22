#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"

#define INITIAL_TOKENS_CAPACITY 16
#define INITIAL_BUF_CAPACITY 64

/**
 * Grow the buffer so it can hold at least `extra` more bytes plus a NUL.
 */
static int strbuf_reserve(strbuf_t *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return 0;
    size_t cap = sb->cap ? sb->cap : INITIAL_BUF_CAPACITY;
    while (sb->len + extra + 1 > cap) cap *= 2;
    char *data = realloc(sb->data, cap);
    if (!data) {
        perror("realloc");
        return -1;
    }
    sb->data = data;
    sb->cap = cap;
    return 0;
}

int strbuf_putc(strbuf_t *sb, char c) {
    if (strbuf_reserve(sb, 1) != 0) return -1;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 0;
}

int strbuf_puts(strbuf_t *sb, const char *s) {
    size_t n = strlen(s);
    if (strbuf_reserve(sb, n) != 0) return -1;
    memcpy(sb->data + sb->len, s, n + 1);
    sb->len += n;
    return 0;
}

/**
 * Free the array of tokens and each token string
 */
void free_tokens(char **tokens) {
    if (!tokens) return;
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

/**
 * Read one token starting at line[*i] into sb.
 * Properly recognizes operators: |, <, >, >>, 2> outside quotes.
 */
static int read_token(const char *line, size_t *i, const shell_state_t *state,
                      strbuf_t *sb, int *keep) {
    int in_single = 0, in_double = 0, quoted = 0;

    while (line[*i] != '\0') {
        char c = line[*i];
        int rc = 0;

        if (c == '\'' && !in_double) {
            in_single = !in_single;
            quoted = 1;
            (*i)++;
        } else if (c == '"' && !in_single) {
            in_double = !in_double;
            quoted = 1;
            (*i)++;
        } else if ((c == ' ' || c == '\t') && !in_single && !in_double) {
            break;
        } else if (!in_single && !in_double &&
                   (c == '|' || c == '<' || c == '>' ||
                    (c == '2' && line[*i + 1] == '>'))) {
            // If we already accumulated word characters, finish that word first
            if (sb->len > 0) {
                break;
            }

            // Otherwise, consume the operator token
            if (c == '2' && line[*i + 1] == '>') {
                rc = strbuf_puts(sb, "2>");
                *i += 2;
            } else if (c == '>' && line[*i + 1] == '>') {
                rc = strbuf_puts(sb, ">>");
                *i += 2;
            } else {
                rc = strbuf_putc(sb, c);
                (*i)++;
            }
            if (rc != 0) return -1;
            break;
        } else if (c == '$' && !in_single) {
            rc = expand_variable(line, i, state, sb);
        } else {
            rc = strbuf_putc(sb, c);
            (*i)++;
        }
        if (rc != 0) return -1;
    }

    if (in_single || in_double) {
        fprintf(stderr, "simpleshell: syntax error: unclosed quote\n");
        return -1;
    }
    *keep = quoted || sb->len > 0;
    return 0;
}

/**
 * Append a copy of tok to the tokens array, growing it as needed.
 */
static int push_token(char ***tokens, size_t *count, size_t *capacity,
                      const char *tok) {
    if (*count + 1 >= *capacity) {
        size_t new_cap = *capacity * 2;
        char **grown = realloc(*tokens, new_cap * sizeof(char *));
        if (!grown) {
            perror("realloc");
            return -1;
        }
        *tokens = grown;
        *capacity = new_cap;
    }
    size_t len = strlen(tok);
    char *copy = malloc(len + 1);
    if (!copy) {
        perror("malloc");
        return -1;
    }
    memcpy(copy, tok, len + 1);
    (*tokens)[(*count)++] = copy;
    (*tokens)[*count] = NULL;
    return 0;
}

/**
 * Tokenize input command line.
 */
char **tokenize_command(const char *line, const shell_state_t *state) {
    size_t capacity = INITIAL_TOKENS_CAPACITY;
    size_t count = 0;
    char **tokens = malloc(capacity * sizeof(char *));
    if (!tokens) {
        perror("malloc");
        return NULL;
    }
    tokens[0] = NULL;

    strbuf_t sb = {0};
    size_t i = 0;
    while (line[i] != '\0') {
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (line[i] == '\0') break;

        int keep = 0;
        sb.len = 0;
        if (strbuf_reserve(&sb, 0) != 0) goto fail;
        sb.data[0] = '\0';
        if (read_token(line, &i, state, &sb, &keep) != 0) goto fail;
        if (keep && push_token(&tokens, &count, &capacity, sb.data) != 0)
            goto fail;
    }

    free(sb.data);
    return tokens;

fail:
    free(sb.data);
    free_tokens(tokens);
    return NULL;
}