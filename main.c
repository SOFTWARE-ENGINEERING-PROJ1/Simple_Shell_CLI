#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shell.h"

typedef struct {
    char **argv;        /* Arguments for execvp / built-in */
    size_t argc;
    char *infile;       /* Input redirection (<) */
    char *outfile;      /* Output redirection (> or >>) */
    int append_out;     /* 1 for >>, 0 for > */
    char *errfile;      /* Stderr redirection (2>) */
} command_t;

/**
 * Apply input, output, and stderr redirections for a command.
 * Returns 0 on success, -1 on failure.
 */
static int apply_redirections(const command_t *cmd) {
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "simpleshell: %s: %s\n",
                    cmd->infile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("simpleshell: dup2 stdin");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append_out ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfile, flags, 0666);
        if (fd < 0) {
            fprintf(stderr, "simpleshell: %s: %s\n",
                    cmd->outfile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("simpleshell: dup2 stdout");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (cmd->errfile) {
        int flags = O_WRONLY | O_CREAT | O_TRUNC;
        int fd = open(cmd->errfile, flags, 0666);
        if (fd < 0) {
            fprintf(stderr, "simpleshell: %s: %s\n",
                    cmd->errfile, strerror(errno));
            return -1;
        }
        if (dup2(fd, STDERR_FILENO) < 0) {
            perror("simpleshell: dup2 stderr");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

/**
 * Parse tokens into an array of commands separated by '|'.
 * Returns number of commands, or -1 on syntax error.
 */
static int parse_pipeline(char **tokens, command_t **cmds_out) {
    size_t total_tokens = 0;
    while (tokens[total_tokens] != NULL) total_tokens++;
    if (total_tokens == 0) return 0;

    // Check for syntax error: pipe at start or end
    if (strcmp(tokens[0], "|") == 0 || strcmp(tokens[total_tokens - 1], "|") == 0) {
        fprintf(stderr, "simpleshell: syntax error near unexpected token '|'\n");
        return -1;
    }

    // Count commands
    size_t num_cmds = 1;
    for (size_t i = 0; i < total_tokens; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            if (i + 1 < total_tokens && strcmp(tokens[i + 1], "|") == 0) {
                fprintf(stderr, "simpleshell: syntax error near unexpected token '|'\n");
                return -1;
            }
            num_cmds++;
        }
    }

    command_t *cmds = calloc(num_cmds, sizeof(command_t));
    if (!cmds) {
        perror("calloc");
        return -1;
    }

    size_t cmd_idx = 0;
    size_t t = 0;

    while (t < total_tokens) {
        // Allocate generous argv capacity for this command stage
        cmds[cmd_idx].argv = malloc((total_tokens + 1) * sizeof(char *));
        cmds[cmd_idx].argc = 0;

        while (t < total_tokens && strcmp(tokens[t], "|") != 0) {
            if (strcmp(tokens[t], "<") == 0) {
                if (++t >= total_tokens || strcmp(tokens[t], "|") == 0) {
                    fprintf(stderr, "simpleshell: syntax error near unexpected token 'newline'\n");
                    goto fail;
                }
                cmds[cmd_idx].infile = tokens[t++];
            } else if (strcmp(tokens[t], ">") == 0) {
                if (++t >= total_tokens || strcmp(tokens[t], "|") == 0) {
                    fprintf(stderr, "simpleshell: syntax error near unexpected token 'newline'\n");
                    goto fail;
                }
                cmds[cmd_idx].outfile = tokens[t++];
                cmds[cmd_idx].append_out = 0;
            } else if (strcmp(tokens[t], ">>") == 0) {
                if (++t >= total_tokens || strcmp(tokens[t], "|") == 0) {
                    fprintf(stderr, "simpleshell: syntax error near unexpected token 'newline'\n");
                    goto fail;
                }
                cmds[cmd_idx].outfile = tokens[t++];
                cmds[cmd_idx].append_out = 1;
            } else if (strcmp(tokens[t], "2>") == 0) {
                if (++t >= total_tokens || strcmp(tokens[t], "|") == 0) {
                    fprintf(stderr, "simpleshell: syntax error near unexpected token 'newline'\n");
                    goto fail;
                }
                cmds[cmd_idx].errfile = tokens[t++];
            } else {
                cmds[cmd_idx].argv[cmds[cmd_idx].argc++] = tokens[t++];
            }
        }

        cmds[cmd_idx].argv[cmds[cmd_idx].argc] = NULL;
        cmd_idx++;
        if (t < total_tokens && strcmp(tokens[t], "|") == 0) {
            t++; // Skip the '|'
        }
    }

    *cmds_out = cmds;
    return (int)num_cmds;

fail:
    for (size_t i = 0; i <= cmd_idx; i++) {
        free(cmds[i].argv);
    }
    free(cmds);
    return -1;
}

/**
 * Execute a single command or multi-stage pipeline with redirections.
 */
static int execute_pipeline(char **tokens, shell_state_t *state) {
    command_t *cmds = NULL;
    int num_cmds = parse_pipeline(tokens, &cmds);
    if (num_cmds < 0) return 2; // Syntax error
    if (num_cmds == 0) return 0;

    // Single built-in command without pipes: execute in parent process
    if (num_cmds == 1 && cmds[0].argv[0] != NULL && is_builtin(cmds[0].argv[0])) {
        int saved_in = -1, saved_out = -1, saved_err = -1;

        if (cmds[0].infile)  saved_in  = dup(STDIN_FILENO);
        if (cmds[0].outfile) saved_out = dup(STDOUT_FILENO);
        if (cmds[0].errfile) saved_err = dup(STDERR_FILENO);

        int status;
        if (apply_redirections(&cmds[0]) != 0) {
            status = 1;
        } else {
            status = run_builtin(cmds[0].argv, state);
            fflush(stdout);
            fflush(stderr);
        }

        // Restore saved file descriptors
        if (saved_in >= 0)  { dup2(saved_in, STDIN_FILENO); close(saved_in); }
        if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
        if (saved_err >= 0) { dup2(saved_err, STDERR_FILENO); close(saved_err); }

        free(cmds[0].argv);
        free(cmds);
        return status;
    }

    // Set up pipes for multi-stage pipelines
    int (*pipes)[2] = NULL;
    if (num_cmds > 1) {
        pipes = malloc((num_cmds - 1) * sizeof(*pipes));
        if (!pipes) {
            perror("malloc");
            for (int i = 0; i < num_cmds; i++) free(cmds[i].argv);
            free(cmds);
            return 1;
        }
        for (int i = 0; i < num_cmds - 1; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("simpleshell: pipe");
                free(pipes);
                for (int k = 0; k < num_cmds; k++) free(cmds[k].argv);
                free(cmds);
                return 1;
            }
        }
    }

    pid_t *pids = malloc(num_cmds * sizeof(pid_t));
    if (!pids) {
        perror("malloc");
        if (pipes) free(pipes);
        for (int i = 0; i < num_cmds; i++) free(cmds[i].argv);
        free(cmds);
        return 1;
    }

    fflush(NULL);

    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();

        if (pids[i] < 0) {
            perror("simpleshell: fork");
            break;
        }

        if (pids[i] == 0) {
            // Child process: connect pipes
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
            }
            if (i < num_cmds - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // CRITICAL: Close all pipe file descriptors in child
            if (pipes) {
                for (int j = 0; j < num_cmds - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            // Apply command-specific redirections (overrides pipes)
            if (apply_redirections(&cmds[i]) != 0) {
                _exit(1);
            }

            if (cmds[i].argv[0] == NULL) {
                _exit(0);
            }

            // Execute built-in or external command
            if (is_builtin(cmds[i].argv[0])) {
                int status = run_builtin(cmds[i].argv, state);
                fflush(stdout); fflush(stderr); _exit(status);
            }

            execvp(cmds[i].argv[0], cmds[i].argv);
            int saved_errno = errno;
            fprintf(stderr, "simpleshell: %s: %s\n",
                    cmds[i].argv[0], strerror(saved_errno));
            _exit(saved_errno == ENOENT ? 127 : 126);
        }
    }

    // CRITICAL: Parent closes all pipe ends to prevent deadlocks and allow EOF
    if (pipes) {
        for (int j = 0; j < num_cmds - 1; j++) {
            close(pipes[j][0]);
            close(pipes[j][1]);
        }
        free(pipes);
    }

    // Wait for all child processes and retrieve exit status of the last command
    int last_status = 0;
    for (int i = 0; i < num_cmds; i++) {
        if (pids[i] <= 0) continue;
        int wait_status;
        pid_t result;
        do {
            result = waitpid(pids[i], &wait_status, 0);
        } while (result == -1 && errno == EINTR);

        if (i == num_cmds - 1 && result != -1) {
            if (WIFEXITED(wait_status)) {
                last_status = WEXITSTATUS(wait_status);
            } else if (WIFSIGNALED(wait_status)) {
                last_status = 128 + WTERMSIG(wait_status);
            }
        }
    }

    free(pids);
    for (int i = 0; i < num_cmds; i++) {
        free(cmds[i].argv);
    }
    free(cmds);

    return last_status;
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

        // 5. Execute pipeline / redirections
        state.last_status = execute_pipeline(tokens, &state);
        free_tokens(tokens);
    }

    free(line);
    return state.exit_code;
}