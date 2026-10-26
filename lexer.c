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
void free_tokens(token_t *tokens) {
    if (!tokens) return;
    for (int i = 0; tokens[i].text != NULL; i++) {
        free(tokens[i].text);
    }
    free(tokens);
}

int token_is_op(const token_t *tok, const char *op) {
    return tok->text && tok->is_op && strcmp(tok->text, op) == 0;
}

/**
 * Return 1 if line[i] starts an unquoted operator: |, <, >, >>, or 2>.
 * "2>" only counts at the start of a word, so `a2>f` is `a2` then `>`.
 */
static int starts_operator(const char *line, size_t i, int at_word_start) {
    char c = line[i];
    if (c == '|' || c == '<' || c == '>') return 1;
    return c == '2' && line[i + 1] == '>' && at_word_start;
}

/**
 * Read one token starting at line[*i] into sb.
 * Recognizes operators |, <, >, >>, 2> only when typed unquoted; quoted
 * text and the results of $-expansion are always plain words (*is_op = 0).
 */
static int read_token(const char *line, size_t *i, const shell_state_t *state,
                      strbuf_t *sb, int *keep, int *is_op) {
    int in_single = 0, in_double = 0, quoted = 0;
    *is_op = 0;

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
                   starts_operator(line, *i, sb->len == 0 && !quoted)) {
            // If we already have a word (even an empty quoted one), end it first
            if (sb->len > 0 || quoted) {
                break;
            }
            *is_op = 1;

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
 * Append a copy of text to the tokens array, growing it as needed.
 */
static int push_token(token_t **tokens, size_t *count, size_t *capacity,
                      const char *text, int is_op) {
    if (*count + 1 >= *capacity) {
        size_t new_cap = *capacity * 2;
        token_t *grown = realloc(*tokens, new_cap * sizeof(token_t));
        if (!grown) {
            perror("realloc");
            return -1;
        }
        *tokens = grown;
        *capacity = new_cap;
    }
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    if (!copy) {
        perror("malloc");
        return -1;
    }
    memcpy(copy, text, len + 1);
    (*tokens)[*count].text = copy;
    (*tokens)[*count].is_op = is_op;
    (*count)++;
    (*tokens)[*count].text = NULL;
    (*tokens)[*count].is_op = 0;
    return 0;
}

/**
 * Tokenize input command line.
 * Returns an array terminated by a token with text == NULL, or NULL on error.
 */
token_t *tokenize_command(const char *line, const shell_state_t *state) {
    size_t capacity = INITIAL_TOKENS_CAPACITY;
    size_t count = 0;
    token_t *tokens = malloc(capacity * sizeof(token_t));
    if (!tokens) {
        perror("malloc");
        return NULL;
    }
    tokens[0].text = NULL;
    tokens[0].is_op = 0;

    strbuf_t sb = {0};
    size_t i = 0;
    while (line[i] != '\0') {
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (line[i] == '\0') break;

        int keep = 0, is_op = 0;
        sb.len = 0;
        if (strbuf_reserve(&sb, 0) != 0) goto fail;
        sb.data[0] = '\0';
        if (read_token(line, &i, state, &sb, &keep, &is_op) != 0) goto fail;
        if (keep && push_token(&tokens, &count, &capacity, sb.data, is_op) != 0)
            goto fail;
    }

    free(sb.data);
    return tokens;

fail:
    free(sb.data);
    free_tokens(tokens);
    return NULL;
}