#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>
#include <sys/types.h>

#define PROMPT "simpleshell> "

/**
 * Shell state shared between the REPL, expansion and built-ins.
 * Passed explicitly rather than kept as hidden globals.
 */
typedef struct {
    int last_status;  /* exit status of the last command, used by $? */
    pid_t pid;        /* the shell's own PID, used by $$ */
    int should_exit;  /* set by the exit built-in */
    int exit_code;    /* status to exit with when should_exit is set */
} shell_state_t;

/**
 * Growable string buffer used while building tokens.
 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

/* lexer.c */

/**
 * Tokenize a command line, honouring single/double quotes and expanding
 * $VAR, ${VAR}, $? and $$ outside single quotes.
 * Returns a NULL-terminated array of strings, or NULL on error.
 */
char **tokenize_command(const char *line, const shell_state_t *state);

/**
 * Free the array of tokens and each token string.
 */
void free_tokens(char **tokens);

/**
 * Append a single character / string to a buffer. Return 0 on success.
 */
int strbuf_putc(strbuf_t *sb, char c);
int strbuf_puts(strbuf_t *sb, const char *s);

/* expand.c */

/**
 * Expand the variable reference starting at line[*i] (which is '$')
 * into sb, advancing *i past it. A lone '$' is copied literally.
 * Returns 0 on success, -1 on a malformed ${...} or allocation failure.
 */
int expand_variable(const char *line, size_t *i, const shell_state_t *state,
                    strbuf_t *sb);

/**
 * Return 1 if name is a valid variable name ([A-Za-z_][A-Za-z0-9_]*).
 */
int is_valid_var_name(const char *name);

/* builtins.c */

/**
 * Return 1 if the command name is a shell built-in.
 */
int is_builtin(const char *name);

/**
 * Run a built-in command. Returns its exit status.
 */
int run_builtin(char **argv, shell_state_t *state);

#endif
