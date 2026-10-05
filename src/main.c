#include "shell.h"
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void ignore_shell_signals(void) {
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

static void initialize_shell(void) {
    pid_t shell_pid = getpid();

    ignore_shell_signals();

    if (setpgid(shell_pid, shell_pid) == -1) {
        if (getpgrp() != shell_pid) {
            perror("setpgid");
            exit(EXIT_FAILURE);
        }
    }

    if (tcsetpgrp(STDIN_FILENO, shell_pid) == -1) {
        perror("tcsetpgrp");
        exit(EXIT_FAILURE);
    }
}

static void print_prompt(void) {
    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd))) {
        printf("shellcraft:%s$ ", cwd);
    } else {
        printf("shellcraft$ ");
    }

    fflush(stdout);
}

static void free_tokens(Token *tokens, int count) {
    for (int i = 0; i < count; i++) {
        free(tokens[i].text);
    }
}

int main(void) {
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "shellcraft: interactive terminal required\n");
        return EXIT_FAILURE;
    }

    initialize_shell();
    init_jobs();

    char line[MAX_CMDLINE];

    while (1) {
        reap_background_jobs();
        print_prompt();

        if (!fgets(line, sizeof(line), stdin)) {
            putchar('\n');
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        if (line[0] == '\0') {
            continue;
        }

        Token tokens[MAX_TOKENS];
        memset(tokens, 0, sizeof(tokens));

        int ntokens = tokenize(line, tokens, MAX_TOKENS);

        if (ntokens < 0) {
            free_tokens(tokens, 0);
            continue;
        }

        if (ntokens == 0) {
            free_tokens(tokens, ntokens);
            continue;
        }

        Pipeline pipeline;

        if (parse_pipeline(tokens, ntokens, &pipeline) == -1) {
            free_tokens(tokens, ntokens);
            continue;
        }

        execute_pipeline(&pipeline, line);

        free_tokens(tokens, ntokens);
    }

    return 0;
}
