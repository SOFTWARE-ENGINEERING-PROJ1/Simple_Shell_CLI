#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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
 * Read the filename after a redirection operator at tokens[*t].
 * The target must exist and be a plain word, not another operator.
 * Returns the filename and advances *t past it, or NULL on syntax error.
 */
static char *redirect_target(const token_t *tokens, size_t total, size_t *t) {
    (*t)++;
    if (*t >= total) {
        fprintf(stderr, "simpleshell: syntax error near unexpected token 'newline'\n");
        return NULL;
    }
    if (tokens[*t].is_op) {
        fprintf(stderr, "simpleshell: syntax error near unexpected token '%s'\n",
                tokens[*t].text);
        return NULL;
    }
    return tokens[(*t)++].text;
}

/**
 * Parse tokens into an array of commands separated by '|'.
 * Only unquoted operators typed by the user count; quoted text and
 * expanded variable values are always passed through as arguments.
 * Returns number of commands, or -1 on syntax error.
 */
static int parse_pipeline(const token_t *tokens, command_t **cmds_out) {
    size_t total_tokens = 0;
    while (tokens[total_tokens].text != NULL) total_tokens++;
    if (total_tokens == 0) return 0;

    // Check for syntax error: pipe at start or end
    if (token_is_op(&tokens[0], "|") || token_is_op(&tokens[total_tokens - 1], "|")) {
        fprintf(stderr, "simpleshell: syntax error near unexpected token '|'\n");
        return -1;
    }

    // Count commands
    size_t num_cmds = 1;
    for (size_t i = 0; i < total_tokens; i++) {
        if (token_is_op(&tokens[i], "|")) {
            if (i + 1 < total_tokens && token_is_op(&tokens[i + 1], "|")) {
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
        if (!cmds[cmd_idx].argv) {
            perror("malloc");
            goto fail;
        }
        cmds[cmd_idx].argc = 0;

        while (t < total_tokens && !token_is_op(&tokens[t], "|")) {
            command_t *cmd = &cmds[cmd_idx];
            char **slot = NULL;

            if (token_is_op(&tokens[t], "<")) {
                slot = &cmd->infile;
            } else if (token_is_op(&tokens[t], ">")) {
                slot = &cmd->outfile;
                cmd->append_out = 0;
            } else if (token_is_op(&tokens[t], ">>")) {
                slot = &cmd->outfile;
                cmd->append_out = 1;
            } else if (token_is_op(&tokens[t], "2>")) {
                slot = &cmd->errfile;
            } else {
                cmd->argv[cmd->argc++] = tokens[t++].text;
                continue;
            }
            *slot = redirect_target(tokens, total_tokens, &t);
            if (!*slot) goto fail;
        }

        cmds[cmd_idx].argv[cmds[cmd_idx].argc] = NULL;
        cmd_idx++;
        if (t < total_tokens && token_is_op(&tokens[t], "|")) {
            t++; // Skip the '|'
        }
    }

    *cmds_out = cmds;
    return (int)num_cmds;

fail:
    for (size_t i = 0; i <= cmd_idx && i < num_cmds; i++) {
        free(cmds[i].argv);
    }
    free(cmds);
    return -1;
}

/**
 * Free the argv arrays and the command array from parse_pipeline().
 */
static void free_commands(command_t *cmds, int num_cmds) {
    for (int i = 0; i < num_cmds; i++) free(cmds[i].argv);
    free(cmds);
}

/**
 * Run a single built-in in the shell process (so cd/export/exit affect the
 * shell), applying its redirections and restoring stdio afterwards.
 */
static int run_builtin_in_parent(command_t *cmd, shell_state_t *state) {
    int saved_in = -1, saved_out = -1, saved_err = -1;
    int status;

    if ((cmd->infile && (saved_in = dup(STDIN_FILENO)) < 0) ||
        (cmd->outfile && (saved_out = dup(STDOUT_FILENO)) < 0) ||
        (cmd->errfile && (saved_err = dup(STDERR_FILENO)) < 0)) {
        perror("simpleshell: dup");
        status = 1;
    } else if (apply_redirections(cmd) != 0) {
        status = 1;
    } else {
        status = run_builtin(cmd->argv, state);
    }
    fflush(stdout);
    fflush(stderr);

    // Restore saved file descriptors
    if (saved_in >= 0)  { dup2(saved_in, STDIN_FILENO); close(saved_in); }
    if (saved_out >= 0) { dup2(saved_out, STDOUT_FILENO); close(saved_out); }
    if (saved_err >= 0) { dup2(saved_err, STDERR_FILENO); close(saved_err); }
    return status;
}

/**
 * Close the first `count` pipes and free the array.
 */
static void close_pipes(int (*pipes)[2], int count) {
    if (!pipes) return;
    for (int j = 0; j < count; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }
    free(pipes);
}

/**
 * Create num_cmds - 1 pipes. On failure, closes any already created.
 * Returns 0 on success (*out may be NULL for a single command), -1 on error.
 */
static int create_pipes(int num_cmds, int (**out)[2]) {
    *out = NULL;
    if (num_cmds < 2) return 0;

    int (*pipes)[2] = malloc((size_t)(num_cmds - 1) * sizeof(*pipes));
    if (!pipes) {
        perror("simpleshell: malloc");
        return -1;
    }
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("simpleshell: pipe");
            close_pipes(pipes, i);
            return -1;
        }
    }
    *out = pipes;
    return 0;
}

/**
 * Child side of one pipeline stage: restore default signals, connect
 * pipes, apply redirections, then run the built-in or exec the program.
 * Never returns.
 */
static void run_stage(command_t *cmds, int i, int num_cmds, int (*pipes)[2],
                      shell_state_t *state) {
    signals_child_default();

    if ((i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) < 0) ||
        (i < num_cmds - 1 && dup2(pipes[i][1], STDOUT_FILENO) < 0)) {
        perror("simpleshell: dup2");
        _exit(1);
    }
    // CRITICAL: Close all pipe file descriptors in child
    for (int j = 0; pipes && j < num_cmds - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
    }

    // Apply command-specific redirections (overrides pipes)
    if (apply_redirections(&cmds[i]) != 0) _exit(1);
    if (cmds[i].argv[0] == NULL) _exit(0);

    if (is_builtin(cmds[i].argv[0])) {
        int status = run_builtin(cmds[i].argv, state);
        fflush(stdout); fflush(stderr); _exit(status);
    }

    execvp(cmds[i].argv[0], cmds[i].argv);
    int saved_errno = errno;
    if (saved_errno == ENOENT && strchr(cmds[i].argv[0], '/') == NULL) {
        fprintf(stderr, "simpleshell: %s: command not found\n", cmds[i].argv[0]);
    } else {
        fprintf(stderr, "simpleshell: %s: %s\n", cmds[i].argv[0], strerror(saved_errno));
    }
    _exit(saved_errno == ENOENT ? 127 : 126);
}

/**
 * Wait for every started stage. Returns the final stage's status (FR-06.4):
 * its exit code, 128+signal if killed, or 128+signal if stopped by Ctrl-Z.
 * Stopped stages are remembered so they can be cleaned up later.
 */
static int wait_pipeline(const pid_t *pids, int num_cmds, const char *name,
                         const shell_state_t *state) {
    int last_status = 0, interrupted = 0, stopped = 0;

    for (int i = 0; i < num_cmds; i++) {
        int ws = 0;
        if (pids[i] <= 0 || wait_child(pids[i], &ws) == -1) continue;

        int status = 0;
        if (WIFEXITED(ws)) {
            status = WEXITSTATUS(ws);
        } else if (WIFSIGNALED(ws)) {
            status = 128 + WTERMSIG(ws);
            interrupted |= WTERMSIG(ws) == SIGINT;
        } else if (WIFSTOPPED(ws)) {
            status = 128 + WSTOPSIG(ws);
            stopped_add(pids[i]);
            stopped = 1;
        }
        if (i == num_cmds - 1) last_status = status;
    }

    if (stopped) {
        fprintf(stderr, "\nsimpleshell: [stopped] %s: suspended by Ctrl-Z. "
                "Job control (fg/bg/jobs) is not supported; it will be "
                "terminated when the shell exits.\n", name ? name : "command");
    } else if (interrupted && state->interactive) {
        fputc('\n', stdout);
    }
    return last_status;
}

/**
 * Fork one child per stage. Stops early if fork() fails; unstarted
 * stages keep pid -1. Returns 0 on success, -1 if any fork failed.
 */
static int start_stages(command_t *cmds, int num_cmds, int (*pipes)[2],
                        pid_t *pids, shell_state_t *state) {
    fflush(NULL);
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("simpleshell: fork");
            return -1;
        }
        if (pids[i] == 0) run_stage(cmds, i, num_cmds, pipes, state);
    }
    return 0;
}

/**
 * Execute a single command or multi-stage pipeline with redirections.
 */
static int execute_pipeline(const token_t *tokens, shell_state_t *state) {
    command_t *cmds = NULL;
    int num_cmds = parse_pipeline(tokens, &cmds);
    if (num_cmds < 0) return 2; // Syntax error
    if (num_cmds == 0) return 0;

    // Single built-in command without pipes: execute in parent process
    if (num_cmds == 1 && cmds[0].argv[0] != NULL && is_builtin(cmds[0].argv[0])) {
        int status = run_builtin_in_parent(&cmds[0], state);
        free_commands(cmds, num_cmds);
        return status;
    }

    int (*pipes)[2] = NULL;
    pid_t *pids = malloc((size_t)num_cmds * sizeof(pid_t));
    if (!pids || create_pipes(num_cmds, &pipes) != 0) {
        if (!pids) perror("simpleshell: malloc");
        free(pids);
        free_commands(cmds, num_cmds);
        return 1;
    }
    for (int i = 0; i < num_cmds; i++) pids[i] = -1;

    int fork_failed = start_stages(cmds, num_cmds, pipes, pids, state) != 0;

    // CRITICAL: Parent closes all pipe ends to prevent deadlocks and allow EOF
    close_pipes(pipes, num_cmds - 1);

    // Reap every child that did start, even if a later fork failed
    int status = wait_pipeline(pids, num_cmds, cmds[0].argv[0], state);

    free(pids);
    free_commands(cmds, num_cmds);
    return fork_failed ? 1 : status;
}

/**
 * Read one line, retrying if a signal interrupts the read.
 * Returns the getline() result (-1 only on real EOF or error).
 */
static ssize_t read_line(char **line, size_t *len, const shell_state_t *state) {
    ssize_t nread;

    if (state->interactive) {
        printf("%s", PROMPT);
        fflush(stdout);
    }
    signals_set_at_prompt(1);
    do {
        errno = 0;
        clearerr(stdin);
        nread = getline(line, len, stdin);
    } while (nread == -1 && errno == EINTR);
    signals_set_at_prompt(0);
    return nread;
}

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    shell_state_t state = {0, getpid(), 0, 0, isatty(STDIN_FILENO)};

    signals_init(state.interactive);

    while (!state.should_exit) {
        stopped_reap();
        nread = read_line(&line, &len, &state);

        // 1. Handle EOF
        if (nread == -1) {
            if (state.interactive) printf("\nExiting shell...\n");
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
        token_t *tokens = tokenize_command(line, &state);
        if (!tokens) {
            state.last_status = 2;
            continue;
        }
        if (tokens[0].text == NULL) {
            free_tokens(tokens);
            continue;
        }

        // 5. Execute pipeline / redirections
        state.last_status = execute_pipeline(tokens, &state);
        free_tokens(tokens);
    }

    // Never leave suspended children behind (no job control in v1.0)
    stopped_cleanup();
    free(line);
    return state.exit_code;
}
