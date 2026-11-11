#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "shell.h"

static const char *BUILTINS[] = {
    "cd", "pwd", "echo", "export", "unset", "help", "exit", NULL
};

static char *command_generator(const char *text, int state) {
    static int builtin_idx;
    static size_t text_len;
    static char *path_copy;
    static char *saveptr;
    static char *dir;
    static DIR *dp;

    if (!state) {
        builtin_idx = 0;
        text_len = strlen(text);
        if (path_copy) { free(path_copy); path_copy = NULL; }
        if (dp) { closedir(dp); dp = NULL; }
        const char *env = getenv("PATH");
        if (env) {
            path_copy = strdup(env);
            dir = strtok_r(path_copy, ":", &saveptr);
        } else {
            dir = NULL;
        }
    }

    // 1. Check built-ins
    while (BUILTINS[builtin_idx] != NULL) {
        const char *b = BUILTINS[builtin_idx++];
        if (strncmp(b, text, text_len) == 0) {
            return strdup(b);
        }
    }

    // 2. Check PATH executables
    while (dir != NULL || dp != NULL) {
        if (!dp) {
            if (!dir) break;
            dp = opendir(dir);
            if (!dp) {
                dir = strtok_r(NULL, ":", &saveptr);
                continue;
            }
        }

        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (strncmp(de->d_name, text, text_len) == 0) {
                char path[4096];
                snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
                if (access(path, X_OK) == 0) {
                    return strdup(de->d_name);
                }
            }
        }
        closedir(dp);
        dp = NULL;
        dir = strtok_r(NULL, ":", &saveptr);
    }

    if (path_copy) { free(path_copy); path_copy = NULL; }
    return NULL;
}

static char **shell_completion(const char *text, int start, int end) {
    (void)end;

    // Check if we are completing the command (first word)
    int first_word = 1;
    for (int i = 0; i < start; i++) {
        if (rl_line_buffer[i] != ' ' && rl_line_buffer[i] != '\t') {
            first_word = 0;
            break;
        }
    }

    if (first_word) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, command_generator);
    }

    // Not first word: let Readline do default filename and directory completion!
    rl_attempted_completion_over = 0;
    return NULL;
}

void interactive_init(void) {
    rl_attempted_completion_function = shell_completion;
    rl_catch_signals = 0;
    rl_variable_bind("enable-bracketed-paste", "off");
}

char *interactive_read_line(const char *prompt) {
    return readline(prompt);
}

void interactive_add_history(const char *line) {
    if (line && *line != '\0') {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0') {
            add_history(line);
        }
    }
}
