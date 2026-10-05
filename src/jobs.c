#include "shell.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static Job jobs[MAX_JOBS];
static volatile sig_atomic_t child_changed = 0;

static void sigchld_handler(int signal_number) {
    (void)signal_number;
    child_changed = 1;
}

void init_jobs(void) {
    memset(jobs, 0, sizeof(jobs));

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
    }
}

int add_job(
    pid_t pid,
    pid_t pgid,
    const char *command,
    JobState state
) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].id = i + 1;
            jobs[i].pid = pid;
            jobs[i].pgid = pgid;
            jobs[i].state = state;

            snprintf(
                jobs[i].command,
                sizeof(jobs[i].command),
                "%s",
                command
            );

            printf(
                "[%d] %s %d %s\n",
                jobs[i].id,
                state == JOB_STOPPED ? "Stopped" : "Running",
                (int)pid,
                command
            );

            fflush(stdout);

            return jobs[i].id;
        }
    }

    fprintf(stderr, "shellcraft: job table full\n");
    return -1;
}

Job *find_job(int id) {
    if (id <= 0) {
        return NULL;
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid != 0 && jobs[i].id == id) {
            return &jobs[i];
        }
    }

    return NULL;
}

int remove_job_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == pid) {
            jobs[i].pid = 0;
            jobs[i].pgid = 0;
            jobs[i].state = JOB_RUNNING;
            jobs[i].command[0] = '\0';
            return 1;
        }
    }

    return 0;
}

void reap_background_jobs(void) {
    if (!child_changed) {
        return;
    }

    child_changed = 0;

    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].pid == pid) {
                if (WIFEXITED(status)) {
                    printf(
                        "[%d] Done %s\n",
                        jobs[i].id,
                        jobs[i].command
                    );

                    remove_job_by_pid(pid);
                } else if (WIFSIGNALED(status)) {
                    printf(
                        "[%d] Terminated %s\n",
                        jobs[i].id,
                        jobs[i].command
                    );

                    remove_job_by_pid(pid);
                }

                fflush(stdout);
                break;
            }
        }
    }
}

void print_jobs(void) {
    reap_background_jobs();

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid != 0) {
            printf(
                "[%d] %s %d %s\n",
                jobs[i].id,
                jobs[i].state == JOB_STOPPED
                    ? "Stopped"
                    : "Running",
                (int)jobs[i].pid,
                jobs[i].command
            );
        }
    }
}

int bring_job_foreground(int id) {
    Job *job = find_job(id);

    if (!job) {
        fprintf(
            stderr,
            "shellcraft: fg: job %d not found\n",
            id
        );
        return 1;
    }

    if (tcsetpgrp(STDIN_FILENO, job->pgid) == -1) {
        perror("tcsetpgrp");
        return 1;
    }

    if (job->state == JOB_STOPPED) {
        if (kill(-job->pgid, SIGCONT) == -1) {
            perror("SIGCONT");
            tcsetpgrp(STDIN_FILENO, getpgrp());
            return 1;
        }

        job->state = JOB_RUNNING;
    }

    int status;

    while (1) {
        pid_t result = waitpid(
            -job->pgid,
            &status,
            WUNTRACED
        );

        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == ECHILD) {
                break;
            }

            perror("waitpid");
            break;
        }

        if (WIFSTOPPED(status)) {
            job->state = JOB_STOPPED;

            printf(
                "\n[%d] Stopped %s\n",
                job->id,
                job->command
            );

            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            remove_job_by_pid(job->pid);
            break;
        }
    }

    if (tcsetpgrp(STDIN_FILENO, getpgrp()) == -1) {
        perror("tcsetpgrp");
    }

    return 0;
}

int continue_job_background(int id) {
    Job *job = find_job(id);

    if (!job) {
        fprintf(
            stderr,
            "shellcraft: bg: job %d not found\n",
            id
        );
        return 1;
    }

    if (job->state != JOB_STOPPED) {
        printf(
            "[%d] Already running %s\n",
            job->id,
            job->command
        );
        return 0;
    }

    if (kill(-job->pgid, SIGCONT) == -1) {
        perror("SIGCONT");
        return 1;
    }

    job->state = JOB_RUNNING;

    printf(
        "[%d] Running %s\n",
        job->id,
        job->command
    );

    fflush(stdout);

    return 0;
}
