#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern char **environ;

typedef int (*builtin_fn)(char **argv, shell_state_t *state);

typedef struct {
    const char *name;
    builtin_fn fn;
    const char *usage;
} builtin_t;

static int builtin_cd(char **argv, shell_state_t *state);
static int builtin_pwd(char **argv, shell_state_t *state);
static int builtin_echo(char **argv, shell_state_t *state);
static int builtin_export(char **argv, shell_state_t *state);
static int builtin_unset(char **argv, shell_state_t *state);
static int builtin_help(char **argv, shell_state_t *state);
static int builtin_exit(char **argv, shell_state_t *state);

static const builtin_t BUILTINS[] = {
    {"cd", builtin_cd, "cd [dir]        Change directory (default: $HOME)"},
    {"pwd", builtin_pwd, "pwd             Print the current working directory"},
    {"echo", builtin_echo, "echo [args...]  Print arguments separated by spaces"},
    {"export", builtin_export, "export VAR=val  Set an environment variable"},
    {"unset", builtin_unset, "unset VAR       Remove an environment variable"},
    {"help", builtin_help, "help            Show this help message"},
    {"exit", builtin_exit, "exit [n]        Exit the shell with status n (default 0)"},
};

#define NUM_BUILTINS (sizeof(BUILTINS) / sizeof(BUILTINS[0]))

static const builtin_t *find_builtin(const char *name) {
    for (size_t i = 0; i < NUM_BUILTINS; i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    }
    return NULL;
}

int is_builtin(const char *name) {
    return name && find_builtin(name) != NULL;
}

int run_builtin(char **argv, shell_state_t *state) {
    const builtin_t *b = find_builtin(argv[0]);
    return b ? b->fn(argv, state) : 1;
}

/**
 * cd [dir]: change the shell's own working directory and update PWD/OLDPWD.
 */
static int builtin_cd(char **argv, shell_state_t *state) {
    (void)state;
    const char *target = argv[1];

    if (argv[1] && argv[2]) {
        fprintf(stderr, "simpleshell: cd: too many arguments\n");
        return 1;
    }
    if (!target) {
        target = getenv("HOME");
        if (!target || *target == '\0') {
            fprintf(stderr, "simpleshell: cd: HOME not set\n");
            return 1;
        }
    }

    char oldpwd[PATH_MAX];
    int have_old = getcwd(oldpwd, sizeof(oldpwd)) != NULL;

    if (chdir(target) != 0) {
        fprintf(stderr, "simpleshell: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    char newpwd[PATH_MAX];
    if (have_old) setenv("OLDPWD", oldpwd, 1);
    if (getcwd(newpwd, sizeof(newpwd)) != NULL) setenv("PWD", newpwd, 1);
    return 0;
}

/**
 * pwd: print the current working directory.
 */
static int builtin_pwd(char **argv, shell_state_t *state) {
    (void)argv;
    (void)state;
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "simpleshell: pwd: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", cwd);
    return 0;
}

/**
 * echo [args]: print arguments separated by single spaces, then a newline.
 */
static int builtin_echo(char **argv, shell_state_t *state) {
    (void)state;
    for (int i = 1; argv[i] != NULL; i++) {
        if (i > 1) putchar(' ');
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}

/**
 * Print the environment in `export NAME="value"` form.
 */
static void print_environment(void) {
    for (char **env = environ; env && *env; env++) {
        const char *eq = strchr(*env, '=');
        if (!eq) continue;
        printf("export %.*s=\"%s\"\n", (int)(eq - *env), *env, eq + 1);
    }
}

/**
 * Apply a single `NAME=value` (or bare `NAME`) export argument.
 */
static int export_one(const char *arg) {
    const char *eq = strchr(arg, '=');
    size_t name_len = eq ? (size_t)(eq - arg) : strlen(arg);
    char *name = strndup(arg, name_len);
    int status = 0;

    if (!name) {
        perror("strndup");
        return 1;
    }
    if (!is_valid_var_name(name)) {
        fprintf(stderr, "simpleshell: export: '%s': not a valid identifier\n", arg);
        status = 1;
    } else if (eq && setenv(name, eq + 1, 1) != 0) {
        fprintf(stderr, "simpleshell: export: %s\n", strerror(errno));
        status = 1;
    }
    free(name);
    return status;
}

/**
 * export VAR=val ...: set environment variables. With no arguments,
 * list the current environment.
 */
static int builtin_export(char **argv, shell_state_t *state) {
    (void)state;
    int status = 0;

    if (!argv[1]) {
        print_environment();
        return 0;
    }
    for (int i = 1; argv[i] != NULL; i++) {
        if (export_one(argv[i]) != 0) status = 1;
    }
    return status;
}

/**
 * unset VAR ...: remove environment variables.
 */
static int builtin_unset(char **argv, shell_state_t *state) {
    (void)state;
    int status = 0;

    for (int i = 1; argv[i] != NULL; i++) {
        if (!is_valid_var_name(argv[i])) {
            fprintf(stderr, "simpleshell: unset: '%s': not a valid identifier\n",
                    argv[i]);
            status = 1;
        } else if (unsetenv(argv[i]) != 0) {
            fprintf(stderr, "simpleshell: unset: %s\n", strerror(errno));
            status = 1;
        }
    }
    return status;
}

/**
 * help: list the built-in commands.
 */
static int builtin_help(char **argv, shell_state_t *state) {
    (void)argv;
    (void)state;
    printf("SimpleShell built-in commands:\n");
    for (size_t i = 0; i < NUM_BUILTINS; i++) {
        printf("  %s\n", BUILTINS[i].usage);
    }
    printf("External commands are supported.\n");
    return 0;
}

/**
 * exit [n]: request shell exit with status n (default 0).
 * A non-integer n exits with status 1, per the SRS.
 */
static int builtin_exit(char **argv, shell_state_t *state) {
    int code = 0;

    if (argv[1] && argv[2]) {
        fprintf(stderr, "simpleshell: exit: too many arguments\n");
        return 1;
    }
    if (argv[1]) {
        char *end;
        errno = 0;
        long value = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0') {
            fprintf(stderr, "simpleshell: exit: %s: numeric argument required\n",
                    argv[1]);
            code = 1;
        } else {
            code = (int)(value & 0xFF);
        }
    }
    state->should_exit = 1;
    state->exit_code = code;
    return code;
}
