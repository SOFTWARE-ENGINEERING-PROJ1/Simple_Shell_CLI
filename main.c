#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"

/**
 * Run one tokenized command and return its exit status.
 * Built-ins run in the shell process; external execution is added in S3.
 */
static int execute_command(char **argv, shell_state_t *state) {
    if (is_builtin(argv[0])) {
        int status = run_builtin(argv, state);
        fflush(stdout);
        return status;
    }
    fprintf(stderr, "simpleshell: %s: command not found\n", argv[0]);
    return 127;
}

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    int interactive = isatty(STDIN_FILENO);
    shell_state_t state = {0, getpid(), 0, 0};

    while (!state.should_exit) {
        if (interactive) {
            printf("%s", PROMPT);
            fflush(stdout);
        }

        nread = getline(&line, &len, stdin);

        // 1. Handle EOF
        if (nread == -1) {
            if (interactive) printf("\nExiting shell...\n");
            state.exit_code = 0;
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

        // 4. Tokenize and expand variables
        char **tokens = tokenize_command(line, &state);
        if (!tokens) {
            state.last_status = 2;
            continue;
        }
        if (tokens[0] == NULL) {
            free_tokens(tokens);
            continue;
        }

        // 5. Execute
        state.last_status = execute_command(tokens, &state);
        free_tokens(tokens);
    }

    free(line);
    return state.exit_code;
}
