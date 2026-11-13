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
#include <termios.h>
#include "shell.h"

/* 1 while the shell is waiting at the prompt (read by the signal handler). */
static volatile sig_atomic_t g_at_prompt = 0;
static volatile sig_atomic_t g_show_prompt = 0;
static pid_t shell_pgid = -1;
static int shell_terminal = STDIN_FILENO;
static int job_control_enabled = 0;
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
static volatile sig_atomic_t g_prompt_interrupted = 0;

static void shell_signal_handler(int sig) {
    (void)sig;

    if (g_at_prompt && g_show_prompt) {
        g_prompt_interrupted = 1;
    }
}

int signals_take_prompt_interrupt(void) {
    sig_atomic_t interrupted = g_prompt_interrupted;
    g_prompt_interrupted = 0;
    return interrupted;
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
void job_control_init(void) {

    if (!isatty(shell_terminal)) {
        job_control_enabled = 0;
        return;
    }

    shell_pgid = getpid();

    /* Prevent the shell from being stopped while changing terminal owner. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);


    if (setpgid(shell_pgid, shell_pgid) < 0 &&
        errno != EACCES && errno != EPERM) {
        perror("simpleshell: setpgid");
    }

    shell_pgid = getpgrp();

    if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
        perror("simpleshell: tcsetpgrp");
        return;
    }

    job_control_enabled = 1;
}
void job_control_give_terminal(pid_t pgid) {
    if (job_control_enabled && pgid > 0) {
        if (tcsetpgrp(shell_terminal, pgid) < 0) {
            perror("simpleshell: tcsetpgrp");
        }
    }
}

void job_control_restore_terminal(void) {
    if (job_control_enabled && shell_pgid > 0) {
        if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
            perror("simpleshell: restore terminal");
        }
    }
}

int job_control_wait(job_t *job) {
    if (!job) {
        return 1;
    }

    int last_status = job->last_status;

    while (job->completed_count + job->stopped_count <
           job->process_count) {

        int status = 0;
        pid_t pid = waitpid(-job->pgid, &status, WUNTRACED);

        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == ECHILD) {
                break;
            }

            perror("simpleshell: waitpid");
            return 1;
        }

        for (int i = 0; i < job->process_count; i++) {
            if (job->pids[i] != pid) {
                continue;
            }

            if (WIFSTOPPED(status)) {
                if (job->states[i] != 1) {
                    job->states[i] = 1;
                    job->stopped_count++;
                }

                if (i == job->process_count - 1) {
                    last_status = 128 + WSTOPSIG(status);
                }
            } else if (WIFEXITED(status)) {
                if (job->states[i] != 2) {
                    if (job->states[i] == 1) {
                        job->stopped_count--;
                    }

                    job->states[i] = 2;
                    job->completed_count++;
                }

                job->pids[i] = -1;

                if (i == job->process_count - 1) {
                    last_status = WEXITSTATUS(status);
                }
            } else if (WIFSIGNALED(status)) {
                if (job->states[i] != 2) {
                    if (job->states[i] == 1) {
                        job->stopped_count--;
                    }

                    job->states[i] = 2;
                    job->completed_count++;
                }

                job->pids[i] = -1;

                if (i == job->process_count - 1) {
                    last_status = 128 + WTERMSIG(status);
                }
            }

            break;
        }
    }

    job->last_status = last_status;
    return last_status;
}
void signals_child_default(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
sigaction(SIGTTOU, &sa, NULL);
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
