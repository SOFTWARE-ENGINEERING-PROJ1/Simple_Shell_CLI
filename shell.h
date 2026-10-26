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
    int interactive;  /* 1 when stdin is a terminal */
} shell_state_t;

/**
 * Growable string buffer used while building tokens.
 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

/**
 * One lexer token. is_op is 1 only for an operator the user typed unquoted
 * (|, <, >, >>, 2>); quoted text and expansion results are always words,
 * so a variable holding "|" can never become a pipe.
 */
typedef struct {
    char *text;
    int is_op;
} token_t;

/* lexer.c */

/**
 * Tokenize a command line, honouring single/double quotes and expanding
 * $VAR, ${VAR}, $? and $$ outside single quotes.
 * Returns an array terminated by a token whose text is NULL, or NULL on error.
 */
token_t *tokenize_command(const char *line, const shell_state_t *state);

/**
 * Free the array of tokens and each token string.
 */
void free_tokens(token_t *tokens);

/**
 * Return 1 if tok is the unquoted operator op (e.g. "|").
 */
int token_is_op(const token_t *tok, const char *op);

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

/* signals.c */

/**
 * Install the shell's SIGINT/SIGTSTP handlers so Ctrl-C and Ctrl-Z never
 * terminate or suspend the shell itself (FR-07.1-07.3).
 * When interactive, Ctrl-C/Ctrl-Z at an idle prompt redraw the prompt.
 */
void signals_init(int interactive);

/**
 * Restore default SIGINT/SIGTSTP in a forked child, so the foreground
 * command (including a built-in run inside a pipeline) can be
 * interrupted or suspended.
 */
void signals_child_default(void);

/**
 * Tell the signal handler whether the shell is idle at its prompt.
 */
void signals_set_at_prompt(int at_prompt);

/**
 * waitpid() for pid, also reporting stops (WUNTRACED) and retrying on EINTR.
 * Returns pid, or -1 on error (message already printed).
 */
pid_t wait_child(pid_t pid, int *wait_status);

/**
 * Remember a child suspended by Ctrl-Z. Job control (fg/bg/jobs) is out
 * of scope, so it is never resumed; it is only reaped or terminated.
 */
void stopped_add(pid_t pid);

/**
 * Reap any remembered children that have since exited (no zombies).
 */
void stopped_reap(void);

/**
 * Terminate and reap all remembered suspended children (called on exit).
 */
void stopped_cleanup(void);

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
