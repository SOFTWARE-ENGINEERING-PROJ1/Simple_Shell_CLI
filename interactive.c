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
static int shell_readline_event_hook(void) {
    if (signals_take_prompt_interrupt()) {
        rl_replace_line("", 0);
        rl_on_new_line();
        rl_redisplay();
    }

    return 0;
}
static const char *BUILTIN_NAMES[] = {
    "cd", "pwd", "echo", "export", "unset",
    "help", "jobs", "fg", "bg", "exit", NULL
};

static char **g_matches = NULL;
static size_t g_match_count = 0;
static size_t g_match_cap = 0;
static size_t g_match_iter = 0;

static void clear_matches(void) {
    if (g_matches) {
        for (size_t i = 0; i < g_match_count; i++) {
            free(g_matches[i]);
        }
        free(g_matches);
        g_matches = NULL;
    }
    g_match_count = 0;
    g_match_cap = 0;
    g_match_iter = 0;
}

static void add_match(const char *name) {
    for (size_t i = 0; i < g_match_count; i++) {
        if (strcmp(g_matches[i], name) == 0) return;
    }
    if (g_match_count == g_match_cap) {
        size_t new_cap = g_match_cap ? g_match_cap * 2 : 32;
        char **grown = realloc(g_matches, new_cap * sizeof(char *));
        if (!grown) return;
        g_matches = grown;
        g_match_cap = new_cap;
    }
    g_matches[g_match_count++] = strdup(name);
}

static char *command_generator(const char *text, int state) {
    (void)text;
    if (!state) {
        g_match_iter = 0;
    }
    if (g_match_iter < g_match_count) {
        return strdup(g_matches[g_match_iter++]);
    }
    return NULL;
}

static void collect_path_executables(const char *prefix, size_t len) {
    const char *env_path = getenv("PATH");
    if (!env_path) return;

    char *path_copy = strdup(env_path);
    if (!path_copy) return;

    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    while (dir) {
        DIR *dp = opendir(dir);
        if (dp) {
            struct dirent *de;
            while ((de = readdir(dp)) != NULL) {
                if (de->d_name[0] == '.') continue;
                if (strncmp(de->d_name, prefix, len) == 0) {
                    char full[4096];
                    snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
                    if (access(full, X_OK) == 0) {
                        struct stat st;
                        if (stat(full, &st) == 0 && !S_ISDIR(st.st_mode)) {
                            add_match(de->d_name);
                        }
                    }
                }
            }
            closedir(dp);
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(path_copy);
}

static char **shell_completion(const char *text, int start, int end) {
    (void)end;
    clear_matches();

    int is_first_word = 1;
    for (int i = 0; i < start; i++) {
        if (rl_line_buffer[i] != ' ' && rl_line_buffer[i] != '\t') {
            is_first_word = 0;
            break;
        }
    }

    /* Relative/absolute command paths like ./prog or /bin/ls */
    if (strchr(text, '/') != NULL) {
        rl_attempted_completion_over = 0;
        return rl_completion_matches(text, rl_filename_completion_function);
    }

    if (is_first_word) {
        size_t text_len = strlen(text);

        /* 1. Built-in command matches */
        for (int i = 0; BUILTIN_NAMES[i] != NULL; i++) {
            if (strncmp(BUILTIN_NAMES[i], text, text_len) == 0) {
                add_match(BUILTIN_NAMES[i]);
            }
        }

        /* 2. Deduplicated PATH matches */
        collect_path_executables(text, text_len);



rl_attempted_completion_over = 1;
        if (g_match_count == 0) return NULL;
        return rl_completion_matches(text, command_generator);
    }

    /* Filenames and directory names for command arguments */
    rl_attempted_completion_over = 0;
    return rl_completion_matches(text, rl_filename_completion_function);
}

void interactive_init(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    rl_attempted_completion_function = shell_completion;
rl_catch_signals = 0;
rl_event_hook = shell_readline_event_hook;
    rl_variable_bind("enable-bracketed-paste", "off");
    rl_variable_bind("show-all-if-ambiguous", "on");
    rl_variable_bind("completion-query-items", "0");
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
