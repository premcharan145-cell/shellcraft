#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

#define MAX_ARGS 64
#define MAX_TOKENS 128
#define MAX_JOBS 64
#define MAX_CMDLINE 1024
#define MAX_CMD_LEN 256

typedef enum {
    TOK_WORD,
    TOK_PIPE,
    TOK_IN,
    TOK_OUT,
    TOK_APPEND,
    TOK_BG
} TokenType;

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED
} JobState;

typedef struct {
    char *text;
    TokenType type;
} Token;

typedef struct {
    char *argv[MAX_ARGS];
    char *input_file;
    char *output_file;
    int append;
} Command;

typedef struct {
    Command commands[MAX_ARGS];
    int count;
    int background;
} Pipeline;

typedef struct {
    int id;
    pid_t pid;
    pid_t pgid;
    char command[MAX_CMD_LEN];
    JobState state;
} Job;

int tokenize(char *line, Token *tokens, int max_tokens);
int parse_pipeline(Token *tokens, int ntokens, Pipeline *pipeline);
void free_pipeline(Pipeline *pipeline);

void init_jobs(void);
int add_job(pid_t pid, pid_t pgid, const char *command, JobState state);
void reap_background_jobs(void);
void print_jobs(void);
Job *find_job(int id);
int remove_job_by_pid(pid_t pid);

int bring_job_foreground(int id);
int continue_job_background(int id);

int is_builtin(Command *cmd);
int run_builtin(Command *cmd);
int execute_pipeline(Pipeline *pipeline, const char *original_line);

#endif
