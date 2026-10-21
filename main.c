#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "shell.h"




/**
 * Run a built-in in the shell process, or execute an external
 * command in a child process and return its exit status.
 */
static int execute_command(char **argv, shell_state_t *state) {
    if (is_builtin(argv[0])) {
        int status = run_builtin(argv, state);
        fflush(stdout);
        return status;
    }

    /* Flush output before creating the child process. */
    fflush(NULL);

    pid_t pid = fork();

    if (pid < 0) {
        perror("simpleshell: fork");
        return 1;
    }

    if (pid == 0) {
        /* Child process: execute the external command. */
        execvp(argv[0], argv);

        /* execvp returns only when execution fails. */
        int saved_errno = errno;

        fprintf(stderr, "simpleshell: %s: %s\n",
                argv[0], strerror(saved_errno));

        _exit(saved_errno == ENOENT ? 127 : 126);
    }

    /* Parent process: wait for the foreground command. */
    int wait_status;
    pid_t result;

    do {
        result = waitpid(pid, &wait_status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        perror("simpleshell: waitpid");
        return 1;
    }

    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }

    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }

    return 1;
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
