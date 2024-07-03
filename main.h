#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include "builtin.h"
#include "helpers.h"
#include "data_structs.h"

typedef enum
{
    JOB,
    OPERATOR
} Type;

typedef enum Token_Type
{
    CMD,
    ARG,
    REDIRECTION,
    PIPE,
    OPER,
    LINE_CONTINUATION,
    INVERSION,
    QUOTE,
    QUOTE_END,
    BG_OPER,
    END
} Token_Type;

typedef struct wrapper
{
    struct job *j;
    char *oper;
    Type type;
    int exit_status;
} wrapper;

void read_line(char *buffer);
char **tokenize(char *line);
void init_shell();
job *create_job(char **tokens, int start, int end);
void launch_job(job *j, int foreground);
void free_job(job *j);
void do_job_notification();
void wait_for_job(job *j);
void put_job_in_foreground(job *j, int cont);
void put_job_in_background(job *j, int cont);
void format_job_info(job *j, const char *status);
int job_is_stopped(job *j);
int job_is_completed(job *j);
int mark_process_status(pid_t pid, int status);
void update_status();
void mark_job_as_running(job *j);
void continue_job(job *j, int foreground, int send_cont);
job *find_job(pid_t pgid);
wrapper **create_jobs(char **tokens);
int launch_jobs(wrapper **list);
void print_list(wrapper **list);
int execute(job *j, int foreground);
int check_tokens(char **tokens);