
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shell.h"

static job_t *job_list = NULL;
static size_t job_count = 0;
static int next_job_id = 1;

/* Update the stopped process count when a process changes state. */
static void update_stopped_count(job_t *job, int index, int new_state) {
    if (job->states[index] == 1) {
        job->stopped_count--;
    }

    if (new_state == 1) {
        job->stopped_count++;
    }

    job->states[index] = new_state;
}

/* Find a job by its job ID. */
job_t *jobs_find(int job_id) {
    for (size_t i = 0; i < job_count; i++) {
        if (job_list[i].job_id == job_id) {
            return &job_list[i];
        }
    }

    return NULL;
}

/* Register a new background job. */
int jobs_add(pid_t pgid, const pid_t *pids, int process_count,
             const char *command) {
    if (pgid <= 0 || process_count <= 0 || !pids || !command) {
        return -1;
    }

    job_t *grown = realloc(
        job_list, (job_count + 1) * sizeof(job_t)
    );

    if (!grown) {
        perror("simpleshell: realloc jobs");
        return -1;
    }

    job_list = grown;

    job_t *job = &job_list[job_count];
    memset(job, 0, sizeof(*job));

    job->pids = malloc((size_t)process_count * sizeof(pid_t));
    job->states = calloc((size_t)process_count, sizeof(int));
    job->command = strdup(command);

    if (!job->pids || !job->states || !job->command) {
        perror("simpleshell: allocate job");

        free(job->pids);
        free(job->states);
        free(job->command);

        memset(job, 0, sizeof(*job));
        return -1;
    }

    memcpy(job->pids, pids,
           (size_t)process_count * sizeof(pid_t));

    job->job_id = next_job_id++;
    job->pgid = pgid;
    job->process_count = process_count;

    job_count++;

    return job->job_id;
}

/* Remove a job and release its allocated memory. */
void jobs_remove(int job_id) {
    for (size_t i = 0; i < job_count; i++) {
        if (job_list[i].job_id == job_id) {
            free(job_list[i].pids);
            free(job_list[i].states);
            free(job_list[i].command);

            for (size_t j = i; j + 1 < job_count; j++) {
                job_list[j] = job_list[j + 1];
            }

            job_count--;
            return;
        }
    }
}

/* Reap children and update running/stopped/completed states. */
int jobs_reap(void) {
    int completed_jobs = 0;
    for (size_t i = 0; i < job_count; i++) {
        job_t *job = &job_list[i];

        for (int j = 0; j < job->process_count; j++) {
            if (job->pids[j] <= 0 || job->states[j] == 2) {
                continue;
            }

            int status;
            pid_t result = waitpid(
                job->pids[j],
                &status,
                WNOHANG | WUNTRACED | WCONTINUED
            );

            if (result == job->pids[j]) {
                if (WIFSTOPPED(status)) {
                    update_stopped_count(job, j, 1);
                } else if (WIFCONTINUED(status)) {
                    update_stopped_count(job, j, 0);
                } else if (WIFEXITED(status)) {
                    job->last_status = WEXITSTATUS(status);
                    update_stopped_count(job, j, 2);
                    job->completed_count++;
                    job->pids[j] = -1;
                } else if (WIFSIGNALED(status)) {
                    job->last_status = 128 + WTERMSIG(status);
                    update_stopped_count(job, j, 2);
                    job->completed_count++;
                    job->pids[j] = -1;
                }
            } else if (result < 0 && errno == ECHILD) {
                update_stopped_count(job, j, 2);
                job->completed_count++;
                job->pids[j] = -1;
            }
        }
    }

    /* Report and remove completed jobs. */
    size_t i = 0;

    while (i < job_count) {
        if (job_list[i].completed_count == job_list[i].process_count) {
            fprintf(stderr, "[%d] Done %s\n",
                    job_list[i].job_id,
                    job_list[i].command);

            jobs_remove(job_list[i].job_id);
            completed_jobs++;
        } else {
            i++;
        }
    }

    return completed_jobs;
}

/* Display currently running and stopped jobs. */
void jobs_print(void) {
    jobs_reap();

    for (size_t i = 0; i < job_count; i++) {
        job_t *job = &job_list[i];

        const char *status;

        if (job->stopped_count > 0) {
            status = "Stopped";
        } else {
            status = "Running";
        }

        printf("[%d] %-8s %s\n",
               job->job_id,
               status,
               job->command);
    }

    fflush(stdout);
}

/* Terminate and reap all remaining background jobs. */
void jobs_cleanup(void) {
    for (size_t i = 0; i < job_count; i++) {
        if (job_list[i].pgid > 0) {
            kill(-job_list[i].pgid, SIGHUP);
            kill(-job_list[i].pgid, SIGCONT);
            kill(-job_list[i].pgid, SIGTERM);
        }
    }

    for (size_t i = 0; i < job_count; i++) {
        for (int j = 0; j < job_list[i].process_count; j++) {
            if (job_list[i].pids[j] > 0) {
                while (waitpid(job_list[i].pids[j], NULL, 0) < 0 &&
                       errno == EINTR) {
                }
            }
        }

        free(job_list[i].pids);
        free(job_list[i].states);
        free(job_list[i].command);
    }

    free(job_list);

    job_list = NULL;
    job_count = 0;
}
