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

typedef struct process
{
    struct process *next;            /* next process in pipeline */
    char **argv;                     /* for exec */
    pid_t pid;                       /* process ID */
    char completed;                  /* true if process has completed */
    char stopped;                    /* true if process has stopped */
    int status;                      /* reported status value */
    int exit_status;                 /* actual exit status */
    char *infile, *outfile, *errfile; /* i/o channel names */
    int append_mode;                 /* true if appending */
} process;

typedef struct job
{
    struct job *next;          /* next active job */
    char *command;             /* command line, used for messages */
    process *first_process;    /* list of processes in this job */
    pid_t pgid;                /* process group ID */
    char notified;             /* true if user told about stopped job */
    struct termios tmodes;     /* saved terminal modes */
    int stdin, stdout, stderr; /* standard i/o channels */
    int inverted;              /* inversion of the exit status */
    int in_bg;                 /* true if job is running in background. */
} job;

typedef enum
{
    FG_JOB,
    BG_JOB,
    OPERATOR
} Type;

typedef struct wrapper
{
    struct job *j;
    char *oper;
    Type type;
    int exit_status;
} wrapper;

void read_line(char *buffer);
void tokenize(char **buffer, char *line);
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
void continue_job(job *j, int foreground);
job *find_job(pid_t pgid);
wrapper **create_jobs(char **tokens);
int launch_jobs(wrapper **list);
void print_list(wrapper **list);
int execute(job *j, int foreground);