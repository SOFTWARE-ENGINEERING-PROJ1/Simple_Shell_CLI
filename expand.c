#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"

int is_valid_var_name(const char *name) {
    if (!name || !(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return 0;
    for (size_t i = 1; name[i] != '\0'; i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) return 0;
    }
    return 1;
}

/**
 * Append the value of environment variable `name` (length n) to sb.
 * Unset variables expand to the empty string (FR-08.1).
 */
static int append_env(const char *name, size_t n, strbuf_t *sb) {
    char *key = malloc(n + 1);
    if (!key) {
        perror("malloc");
        return -1;
    }
    memcpy(key, name, n);
    key[n] = '\0';
    const char *value = getenv(key);
    free(key);
    return value ? strbuf_puts(sb, value) : 0;
}

/**
 * Handle the ${VAR} form. line[*i] points at '{'.
 */
static int expand_braced(const char *line, size_t *i, strbuf_t *sb) {
    size_t start = *i + 1;
    size_t end = start;
    while (line[end] != '\0' && line[end] != '}') end++;

    if (line[end] != '}') {
        fprintf(stderr, "simpleshell: syntax error: missing '}'\n");
        return -1;
    }
    size_t n = end - start;
    int valid = n > 0 && (isalpha((unsigned char)line[start]) || line[start] == '_');
    for (size_t k = start; valid && k < end; k++) {
        valid = isalnum((unsigned char)line[k]) || line[k] == '_';
    }
    if (!valid) {
        fprintf(stderr, "simpleshell: ${%.*s}: bad substitution\n", (int)n,
                line + start);
        return -1;
    }
    *i = end + 1;
    return append_env(line + start, n, sb);
}

int expand_variable(const char *line, size_t *i, const shell_state_t *state,
                    strbuf_t *sb) {
    char numbuf[32];
    char next = line[*i + 1];

    if (next == '?' || next == '$') {
        long value = next == '?' ? (long)state->last_status : (long)state->pid;
        snprintf(numbuf, sizeof(numbuf), "%ld", value);
        *i += 2;
        return strbuf_puts(sb, numbuf);
    }
    if (next == '{') {
        (*i)++;
        return expand_braced(line, i, sb);
    }
    if (isalpha((unsigned char)next) || next == '_') {
        size_t start = *i + 1;
        size_t end = start;
        while (isalnum((unsigned char)line[end]) || line[end] == '_') end++;
        *i = end;
        return append_env(line + start, end - start, sb);
    }

    /* A '$' not followed by a supported form is kept literally. */
    (*i)++;
    return strbuf_putc(sb, '$');
}
