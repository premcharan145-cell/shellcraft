#include "shell.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tokenize(char *line, Token *tokens, int max_tokens) {
    int count = 0;
    char *p = line;

    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }

        if (!*p) {
            break;
        }

        if (count >= max_tokens - 1) {
            fprintf(stderr, "shellcraft: too many tokens\n");
            return -1;
        }

        if (*p == '|') {
            tokens[count].text = strdup("|");
            tokens[count].type = TOK_PIPE;
            count++;
            p++;
        } else if (*p == '<') {
            tokens[count].text = strdup("<");
            tokens[count].type = TOK_IN;
            count++;
            p++;
        } else if (*p == '>') {
            if (*(p + 1) == '>') {
                tokens[count].text = strdup(">>");
                tokens[count].type = TOK_APPEND;
                count++;
                p += 2;
            } else {
                tokens[count].text = strdup(">");
                tokens[count].type = TOK_OUT;
                count++;
                p++;
            }
        } else if (*p == '&') {
            tokens[count].text = strdup("&");
            tokens[count].type = TOK_BG;
            count++;
            p++;
        } else {
            char buffer[MAX_CMD_LEN];
            size_t n = 0;
            char quote = '\0';

            while (*p) {
                if (quote) {
                    if (*p == quote) {
                        quote = '\0';
                        p++;
                        continue;
                    }

                    if (n < sizeof(buffer) - 1) {
                        buffer[n++] = *p;
                    }

                    p++;
                } else {
                    if (*p == '\'' || *p == '"') {
                        quote = *p;
                        p++;
                    } else if (
                        isspace((unsigned char)*p) ||
                        *p == '|' ||
                        *p == '<' ||
                        *p == '>' ||
                        *p == '&'
                    ) {
                        break;
                    } else {
                        if (n < sizeof(buffer) - 1) {
                            buffer[n++] = *p;
                        }

                        p++;
                    }
                }
            }

            if (quote) {
                fprintf(stderr, "shellcraft: unmatched quote\n");

                for (int i = 0; i < count; i++) {
                    free(tokens[i].text);
                }

                return -1;
            }

            buffer[n] = '\0';

            tokens[count].text = strdup(buffer);
            tokens[count].type = TOK_WORD;
            count++;
        }
    }

    tokens[count].text = NULL;
    tokens[count].type = TOK_WORD;

    return count;
}

static void clear_command(Command *cmd) {
    memset(cmd, 0, sizeof(*cmd));
}

int parse_pipeline(Token *tokens, int ntokens, Pipeline *pipeline) {
    memset(pipeline, 0, sizeof(*pipeline));

    if (ntokens == 0) {
        return 0;
    }

    int command_index = 0;
    int argument_index = 0;

    clear_command(&pipeline->commands[0]);

    for (int i = 0; i < ntokens; i++) {
        Token *token = &tokens[i];

        if (token->type == TOK_WORD) {
            if (argument_index >= MAX_ARGS - 1) {
                fprintf(stderr, "shellcraft: too many arguments\n");
                return -1;
            }

            pipeline->commands[command_index]
                .argv[argument_index++] = token->text;

            pipeline->commands[command_index]
                .argv[argument_index] = NULL;
        }

        else if (token->type == TOK_PIPE) {
            if (argument_index == 0) {
                fprintf(stderr, "shellcraft: invalid pipe placement\n");
                return -1;
            }

            command_index++;
            argument_index = 0;

            clear_command(&pipeline->commands[command_index]);
        }

        else if (
            token->type == TOK_IN ||
            token->type == TOK_OUT ||
            token->type == TOK_APPEND
        ) {
            if (
                i + 1 >= ntokens ||
                tokens[i + 1].type != TOK_WORD
            ) {
                fprintf(stderr, "shellcraft: redirection requires a file\n");
                return -1;
            }

            if (token->type == TOK_IN) {
                pipeline->commands[command_index].input_file =
                    tokens[++i].text;
            } else {
                pipeline->commands[command_index].output_file =
                    tokens[++i].text;

                pipeline->commands[command_index].append =
                    token->type == TOK_APPEND;
            }
        }

        else if (token->type == TOK_BG) {
            if (i != ntokens - 1) {
                fprintf(stderr, "shellcraft: '&' must be at the end\n");
                return -1;
            }

            pipeline->background = 1;
        }
    }

    if (argument_index == 0) {
        fprintf(stderr, "shellcraft: incomplete command\n");
        return -1;
    }

    pipeline->count = command_index + 1;

    return 0;
}

void free_pipeline(Pipeline *pipeline) {
    (void)pipeline;
}
