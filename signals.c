#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shell.h"

/* 1 while the shell is waiting at the prompt (read by the signal handler). */
static volatile sig_atomic_t g_at_prompt = 0;
static volatile sig_atomic_t g_show_prompt = 0;

/* Children suspended by Ctrl-Z. Job control is out of scope, so these are
 * only remembered so they can be reaped or terminated, never resumed. */
static pid_t *g_stopped = NULL;
static size_t g_stopped_count = 0;
static size_t g_stopped_cap = 0;

/**
 * SIGINT/SIGTSTP handler for the shell process itself.
 * The terminal delivers these to the shell and its foreground children
 * together; the children die or stop, while the shell just keeps going.
 * At an idle prompt the terminal has already discarded the typed line, so
 * redraw the prompt. Only async-signal-safe write() is used here.
 */
static void shell_signal_handler(int sig) {
    (void)sig;
    if (g_at_prompt && g_show_prompt) {
        int saved_errno = errno;
        static const char msg[] = "\n" PROMPT;
        ssize_t ignored = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
        errno = saved_errno;
    }
}

void signals_init(int interactive) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = shell_signal_handler;
    sigemptyset(&sa.sa_mask);
    /* SA_RESTART: the prompt's read() resumes instead of failing with EINTR */
    sa.sa_flags = SA_RESTART;

    g_show_prompt = interactive;
    if (sigaction(SIGINT, &sa, NULL) != 0) perror("simpleshell: sigaction SIGINT");
    if (sigaction(SIGTSTP, &sa, NULL) != 0) perror("simpleshell: sigaction SIGTSTP");
}

void signals_child_default(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
}

void signals_set_at_prompt(int at_prompt) {
    g_at_prompt = at_prompt;
}

pid_t wait_child(pid_t pid, int *wait_status) {
    pid_t result;
    do {
        result = waitpid(pid, wait_status, WUNTRACED);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        fprintf(stderr, "simpleshell: waitpid %ld: %s\n", (long)pid, strerror(errno));
    }
    return result;
}

void stopped_add(pid_t pid) {
    if (g_stopped_count == g_stopped_cap) {
        size_t cap = g_stopped_cap ? g_stopped_cap * 2 : 8;
        pid_t *grown = realloc(g_stopped, cap * sizeof(pid_t));
        if (!grown) {
            /* Can't track it: terminate it rather than leave it behind. */
            perror("simpleshell: realloc");
            kill(pid, SIGKILL);
            kill(pid, SIGCONT);
            waitpid(pid, NULL, 0);
            return;
        }
        g_stopped = grown;
        g_stopped_cap = cap;
    }
    g_stopped[g_stopped_count++] = pid;
}

void stopped_reap(void) {
    size_t kept = 0;
    for (size_t i = 0; i < g_stopped_count; i++) {
        int status;
        pid_t r = waitpid(g_stopped[i], &status, WNOHANG);
        /* Keep it while it still exists (stopped, or resumed elsewhere). */
        if (r == 0) g_stopped[kept++] = g_stopped[i];
    }
    g_stopped_count = kept;
}

/**
 * Wait up to ~500 ms for pid to exit after being signalled.
 * Returns 1 if it was reaped.
 */
static int reap_with_timeout(pid_t pid) {
    struct timespec tick = {0, 10 * 1000 * 1000};
    for (int i = 0; i < 50; i++) {
        pid_t r = waitpid(pid, NULL, WNOHANG);
        if (r == pid || (r == -1 && errno == ECHILD)) return 1;
        nanosleep(&tick, NULL);
    }
    return 0;
}

void stopped_cleanup(void) {
    for (size_t i = 0; i < g_stopped_count; i++) {
        pid_t pid = g_stopped[i];
        /* Like a terminal hang-up: SIGHUP, then SIGCONT so it can act on it. */
        kill(pid, SIGHUP);
        kill(pid, SIGCONT);
        if (!reap_with_timeout(pid)) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    }
    free(g_stopped);
    g_stopped = NULL;
    g_stopped_count = g_stopped_cap = 0;
}
