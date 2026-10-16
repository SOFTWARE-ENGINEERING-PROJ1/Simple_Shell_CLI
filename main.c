#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PROMPT "simpleshell> "
#define INITIAL_TOKENS_CAPACITY 16

/**
 * Portable string copy to eliminate compiler warnings
 */
static char *duplicate_string(const char *src) {
    size_t len = strlen(src);
    char *dst = malloc(len + 1);
    if (dst) {
        memcpy(dst, src, len + 1);
    }
    return dst;
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
 * Tokenize input command line.
 * Handles whitespace delimiters, single quotes ('...'), and double quotes ("...").
 * Returns a NULL-terminated array of strings, or NULL on error.
 */
char **tokenize_command(const char *line) {
    size_t capacity = INITIAL_TOKENS_CAPACITY;
    size_t count = 0;
    char **tokens = malloc(capacity * sizeof(char *));
    if (!tokens) {
        perror("malloc");
        return NULL;
    }

    size_t len = strlen(line);
    size_t i = 0;

    // Buffer for building the current token
    char *buf = malloc(len + 1);
    if (!buf) {
        perror("malloc");
        free(tokens);
        return NULL;
    }

    while (i < len) {
        // 1. Skip leading whitespace
        while (i < len && (line[i] == ' ' || line[i] == '\t')) {
            i++;
        }
        if (i >= len) break;

        size_t buf_idx = 0;
        int in_single_quote = 0;
        int in_double_quote = 0;

        // 2. Read characters for this token
        while (i < len) {
            char c = line[i];

            if (c == '\'' && !in_double_quote) {
                in_single_quote = !in_single_quote;
                i++;
            } else if (c == '"' && !in_single_quote) {
                in_double_quote = !in_double_quote;
                i++;
            } else if ((c == ' ' || c == '\t') && !in_single_quote && !in_double_quote) {
                break;
            } else {
                buf[buf_idx++] = c;
                i++;
            }
        }

        // 3. Check for unclosed quote syntax error
        if (in_single_quote || in_double_quote) {
            fprintf(stderr, "simpleshell: syntax error: unclosed quote\n");
            free(buf);
            free_tokens(tokens);
            return NULL;
        }

        buf[buf_idx] = '\0';

        // 4. Expand dynamic array if needed
        if (count + 1 >= capacity) {
            capacity *= 2;
            char **new_tokens = realloc(tokens, capacity * sizeof(char *));
            if (!new_tokens) {
                perror("realloc");
                free(buf);
                free_tokens(tokens);
                return NULL;
            }
            tokens = new_tokens;
        }

        tokens[count] = duplicate_string(buf);
        count++;
    }

    tokens[count] = NULL;
    free(buf);
    return tokens;
}

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    while (1) {
        printf("%s", PROMPT);
        fflush(stdout);

        nread = getline(&line, &len, stdin);

        // 1. Handle EOF
        if (nread == -1) {
            printf("\nExiting shell...\n");
            break;
        }

        // 2. Remove trailing newline
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
            nread--;
        }

        // 3. Ignore empty input
        if (nread == 0) {
            continue;
        }

        // 4. Tokenize
        char **tokens = tokenize_command(line);
        if (!tokens || tokens[0] == NULL) {
            free_tokens(tokens);
            continue;
        }

        // 5. Exit command
        if (strcmp(tokens[0], "exit") == 0 || strcmp(tokens[0], "quit") == 0) {
            printf("Exiting shell...\n");
            free_tokens(tokens);
            break;
        }

        // 6. Print parsed tokens
        for (int i = 0; tokens[i] != NULL; i++) {
            printf("  token[%d] = \"%s\"\n", i, tokens[i]);
        }

        free_tokens(tokens);
    }

    free(line);
    return 0;
}