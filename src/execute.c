#include "shell.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int open_input(const char *file) {
    return open(file, O_RDONLY);
}

static int open_output(const char *file, int append) {
    int flags = O_WRONLY | O_CREAT;

    if (append) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }

    return open(file, flags, 0644);
}

static int setup_redirection(Command *cmd) {
    if (cmd->input_file) {
        int fd = open_input(cmd->input_file);

        if (fd == -1) {
            perror(cmd->input_file);
            return -1;
        }

        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    if (cmd->output_file) {
        int fd = open_output(
            cmd->output_file,
            cmd->append
        );

        if (fd == -1) {
            perror(cmd->output_file);
            return -1;
        }

        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

static void restore_child_signals(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

int is_builtin(Command *cmd) {
    if (!cmd || !cmd->argv[0]) {
        return 0;
    }

    return strcmp(cmd->argv[0], "cd") == 0 ||
           strcmp(cmd->argv[0], "pwd") == 0 ||
           strcmp(cmd->argv[0], "echo") == 0 ||
           strcmp(cmd->argv[0], "jobs") == 0 ||
           strcmp(cmd->argv[0], "fg") == 0 ||
           strcmp(cmd->argv[0], "bg") == 0 ||
           strcmp(cmd->argv[0], "exit") == 0 ||
           strcmp(cmd->argv[0], "help") == 0;
}

int run_builtin(Command *cmd) {
    if (strcmp(cmd->argv[0], "cd") == 0) {
        const char *directory =
            cmd->argv[1] ? cmd->argv[1] : getenv("HOME");

        if (!directory || chdir(directory) == -1) {
            perror("cd");
            return 1;
        }

        return 0;
    }

    if (strcmp(cmd->argv[0], "pwd") == 0) {
        char cwd[4096];

        if (!getcwd(cwd, sizeof(cwd))) {
            perror("pwd");
            return 1;
        }

        puts(cwd);
        return 0;
    }

    if (strcmp(cmd->argv[0], "echo") == 0) {
        for (int i = 1; cmd->argv[i]; i++) {
            if (i > 1) {
                putchar(' ');
            }

            fputs(cmd->argv[i], stdout);
        }

        putchar('\n');
        return 0;
    }

    if (strcmp(cmd->argv[0], "jobs") == 0) {
        print_jobs();
        return 0;
    }

    if (strcmp(cmd->argv[0], "fg") == 0) {
        if (!cmd->argv[1]) {
            fprintf(
                stderr,
                "shellcraft: fg: job number required\n"
            );
            return 1;
        }

        return bring_job_foreground(
            atoi(cmd->argv[1])
        );
    }

    if (strcmp(cmd->argv[0], "bg") == 0) {
        if (!cmd->argv[1]) {
            fprintf(
                stderr,
                "shellcraft: bg: job number required\n"
            );
            return 1;
        }

        return continue_job_background(
            atoi(cmd->argv[1])
        );
    }

    if (strcmp(cmd->argv[0], "help") == 0) {
        puts("Shellcraft commands:");
        puts("  cd [dir]       Change directory");
        puts("  pwd            Print current directory");
        puts("  echo [text]    Print text");
        puts("  jobs           Show jobs");
        puts("  fg [job]       Bring job to foreground");
        puts("  bg [job]       Continue job in background");
        puts("  help           Show help");
        puts("  exit           Exit Shellcraft");
        puts("  < > >> | &     Shell operators");

        return 0;
    }

    if (strcmp(cmd->argv[0], "exit") == 0) {
        exit(0);
    }

    return 1;
}

static int run_builtin_with_redirection(Command *cmd) {
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);

    if (saved_stdin == -1 || saved_stdout == -1) {
        perror("dup");

        if (saved_stdin != -1) {
            close(saved_stdin);
        }

        if (saved_stdout != -1) {
            close(saved_stdout);
        }

        return 1;
    }

    if (setup_redirection(cmd) == -1) {
        if (dup2(saved_stdin, STDIN_FILENO) == -1) {
            perror("dup2");
        }

        if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
            perror("dup2");
        }

        close(saved_stdin);
        close(saved_stdout);

        return 1;
    }

    int result = run_builtin(cmd);

    if (dup2(saved_stdin, STDIN_FILENO) == -1) {
        perror("dup2");
    }

    if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
        perror("dup2");
    }

    close(saved_stdin);
    close(saved_stdout);

    return result;
}

int execute_pipeline(
    Pipeline *pipeline,
    const char *original_line
) {
    if (
        pipeline->count == 1 &&
        !pipeline->background &&
        is_builtin(&pipeline->commands[0])
    ) {
        return run_builtin_with_redirection(
            &pipeline->commands[0]
        );
    }

    int pipes[MAX_ARGS - 1][2];
    pid_t pids[MAX_ARGS];
    pid_t pgid = 0;

    memset(pids, 0, sizeof(pids));

    for (int i = 0; i < pipeline->count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return 1;
        }
    }

    for (int i = 0; i < pipeline->count; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            restore_child_signals();

            if (pgid == 0) {
                pgid = getpid();
            }

            if (setpgid(0, pgid) == -1) {
                perror("setpgid");
                _exit(1);
            }

            if (i > 0) {
                if (dup2(
                    pipes[i - 1][0],
                    STDIN_FILENO
                ) == -1) {
                    perror("dup2");
                    _exit(1);
                }
            }

            if (i < pipeline->count - 1) {
                if (dup2(
                    pipes[i][1],
                    STDOUT_FILENO
                ) == -1) {
                    perror("dup2");
                    _exit(1);
                }
            }

            for (int j = 0; j < pipeline->count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            if (setup_redirection(
                &pipeline->commands[i]
            ) == -1) {
                _exit(1);
            }

            if (is_builtin(&pipeline->commands[i])) {
                int result =
                    run_builtin(&pipeline->commands[i]);

                _exit(result);
            }

            execvp(
                pipeline->commands[i].argv[0],
                pipeline->commands[i].argv
            );

            fprintf(
                stderr,
                "shellcraft: %s: %s\n",
                pipeline->commands[i].argv[0],
                strerror(errno)
            );

            _exit(127);
        }

        if (pgid == 0) {
            pgid = pid;
        }

        if (setpgid(pid, pgid) == -1) {
            if (errno != EACCES && errno != ESRCH) {
                perror("setpgid");
            }
        }

        pids[i] = pid;
    }

    for (int i = 0; i < pipeline->count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (pipeline->background) {
        add_job(
            pids[0],
            pgid,
            original_line,
            JOB_RUNNING
        );

        return 0;
    }

    if (tcsetpgrp(STDIN_FILENO, pgid) == -1) {
        perror("tcsetpgrp");
    }

    int status;
    int stopped = 0;

    while (1) {
        pid_t result = waitpid(
            -pgid,
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
            stopped = 1;
            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            continue;
        }
    }

    if (stopped) {
        int job_id = add_job(
            pids[0],
            pgid,
            original_line,
            JOB_STOPPED
        );

        if (job_id > 0) {
            printf(
                "\n[%d] Stopped %s\n",
                job_id,
                original_line
            );
            fflush(stdout);
        }
    }

    if (tcsetpgrp(STDIN_FILENO, getpgrp()) == -1) {
        perror("tcsetpgrp");
    }

    return 0;
}
