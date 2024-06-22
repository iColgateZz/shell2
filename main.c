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

#define BUF_SIZE 1024
#define TOK_BUF_SIZE 64

typedef struct process
{
    struct process *next; /* next process in pipeline */
    char **argv;          /* for exec */
    pid_t pid;            /* process ID */
    char completed;       /* true if process has completed */
    char stopped;         /* true if process has stopped */
    int status;           /* reported status value */
    int exit_status;      /* actual exit status */
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

job *first_job = NULL;

void read_line(char *buffer);
void tokenize(char **buffer, char *line);
void init_shell();
job *create_job(char **tokens, int start, int end);
int launch_job(job *j, int foreground);
void free_job(job *j);
void do_job_notification();
void wait_for_job(job *j);
int put_job_in_foreground(job *j, int cont);
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

pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;

int main(void)
{
    char *line = malloc(BUF_SIZE * sizeof(char));
    char **tokens = malloc(TOK_BUF_SIZE * sizeof(char *));
    wrapper **list;
    int status = 1;

    /* Make sure the shell is a foreground process. */
    init_shell();

    if (!line)
    {
        fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    if (!tokens)
    {
        fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    do
    {
        /* Regular shell cycle. */
        printf("$ ");
        read_line(line);
        line = trim(line);
        if (line[0] == '\0')
        {
            continue;
        }
        tokenize(tokens, line);
        list = create_jobs(tokens);
        if (list == NULL)
        {
            continue;
        }
        status = launch_jobs(list);
        do_job_notification();

    } while (status);

    free(line);
    free(tokens);
    return 0;
}

void print_list(wrapper **list)
{
    for (int i = 0; list[i] != NULL; i++)
    {
        if (list[i]->type == OPERATOR)
        {
            printf("Type OPERATOR\n");
            printf("Text %s\n", list[i]->oper);
        }
        else
        {
            printf("Type JOB\n");
            printf("Command %s\n", list[i]->j->command);
            printf("PGID %d\n", list[i]->j->pgid);
            printf("\tProcesses: \n");
            process *temp = list[i]->j->first_process;
            do
            {
                int k = 0;
                while (temp->argv[k] != NULL)
                {
                    printf("\tArg %s\n", temp->argv[k]);
                    k++;
                }
                temp = temp->next;
            } while (temp != NULL);
        }
    }
}

int launch_jobs(wrapper **list)
{
    int first = 1, status = 1;
    for (int i = 0; list[i] != NULL; i++)
    {
        if (first)
        {
            first = 0;
            status = execute(list[i]->j, 1);
            if (status == 0)
                break;
            else if (status == 1)
                continue;
            else
            {
                list[i]->exit_status = launch_job(list[i]->j, 1);
                if (list[i]->j->inverted)
                {
                    list[i]->exit_status = !list[i]->exit_status;
                }
            }
        }
        else if (list[i - 1]->type == OPERATOR && list[i]->type == FG_JOB)
        {
            if (strcmp(list[i - 1]->oper, ";") == 0)
            {
                list[i]->exit_status = launch_job(list[i]->j, 1);
                if (list[i]->j->inverted)
                {
                    list[i]->exit_status = !list[i]->exit_status;
                }
            }
            else if (strcmp(list[i - 1]->oper, "&&") == 0)
            {
                if (list[i - 2]->exit_status == EXIT_SUCCESS)
                {
                    list[i]->exit_status = launch_job(list[i]->j, 1);
                    if (list[i]->j->inverted)
                    {
                        list[i]->exit_status = !list[i]->exit_status;
                    }
                }
                else
                {
                    return 1;
                }
            }
            else
            {
                if (list[i - 2]->exit_status != EXIT_SUCCESS)
                {
                    list[i]->exit_status = launch_job(list[i]->j, 1);
                    if (list[i]->j->inverted)
                    {
                        list[i]->exit_status = !list[i]->exit_status;
                    }
                }
                else
                {
                    return 1;
                }
            }
        }
    }
    return status;
}

wrapper *create_job_wrapper(char **tokens, int start, int end)
{
    wrapper *wr = malloc(sizeof(wrapper));
    if (!wr)
    {
        fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    wr->type = FG_JOB;
    wr->j = create_job(tokens, start, end);

    return wr;
}

wrapper *create_oper_wrapper(char *str)
{
    wrapper *wr2 = malloc(sizeof(wrapper));
    if (!wr2)
    {
        fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    wr2->type = OPERATOR;
    wr2->oper = malloc(TOK_BUF_SIZE * sizeof(char));
    if (!wr2->oper)
    {
        fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    wr2->oper = strdup(str);

    return wr2;
}

wrapper **create_jobs(char **tokens)
{
    int start = 0, end = 0, position = 0;
    if (tokens[0] == NULL)
    {
        return NULL; // Empty command
    }
    wrapper **list = malloc(TOK_BUF_SIZE * sizeof(wrapper *));
    if (!list)
    {
        fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    while (tokens[end] != NULL)
    {
        if (isOperator(tokens[end]))
        {
            wrapper *wr = create_job_wrapper(tokens, start, end);
            list[position++] = wr;

            wrapper *wr2 = create_oper_wrapper(tokens[end]);
            list[position++] = wr2;
            start = end + 1;
        }
        else if (endsWith(tokens[end], ';'))
        {
            size_t len = strlen(tokens[end]);
            tokens[end][len - 1] = '\0';
            end++;

            wrapper *wr = create_job_wrapper(tokens, start, end);
            list[position++] = wr;

            wrapper *wr2 = create_oper_wrapper(";");
            list[position++] = wr2;
            start = end;
        }
        end++;
    }
    wrapper *wr = create_job_wrapper(tokens, start, end);
    list[position++] = wr;

    list[position] = NULL;

    return list;
}

job *create_job(char **tokens, int start, int end)
{
    int last_pipe_index = start;
    // Create a new job
    job *j = malloc(sizeof(job));
    if (!j)
    {
        fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    j->stdin = STDIN_FILENO;
    // int fd = open("lol.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    j->stdout = STDOUT_FILENO;
    j->stderr = STDERR_FILENO;
    j->pgid = 0;
    j->notified = 0;
    j->first_process = NULL;
    j->next = NULL;
    j->inverted = 0;
    if (first_job == NULL)
    {
        first_job = j;
    }
    else
    {
        job *temp = first_job;
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        temp->next = j;
    }

    if (strcmp(tokens[start], "!") == 0)
    {
        j->inverted = 1;
        start++;
        last_pipe_index++;
    }
    j->command = strdup(tokens[start]);

    /* Create the processes. */
    for (int i = start; i < end; i++)
    {
        if (strcmp(tokens[i], "|") == 0 || tokens[i + 1] == NULL || i + 1 == end)
        {
            process *p = malloc(sizeof(process));
            if (!p)
            {
                fprintf(stderr, "psh: allocation error\n");
                exit(EXIT_FAILURE);
            }
            p->completed = 0;
            p->stopped = 0;
            p->next = NULL;
            p->argv = malloc(TOK_BUF_SIZE * sizeof(char *));
            if (!p->argv)
            {
                fprintf(stderr, "psh: allocation error\n");
                exit(EXIT_FAILURE);
            }
            int position = 0;
            for (int j = last_pipe_index; j <= i; j++)
            {
                if (strcmp(tokens[j], "|") != 0)
                {
                    p->argv[position++] = strdup(tokens[j]);
                }
            }
            p->argv[position] = NULL;
            last_pipe_index = i + 1;
            if (j->first_process == NULL)
            {
                j->first_process = p;
            }
            else
            {
                process *proc = j->first_process;
                while (proc->next != NULL)
                {
                    proc = proc->next;
                }
                proc->next = p;
            }
        }
    }

    return j;
}

void read_line(char *buffer)
{
    int position = 0;
    int c;

    while (1)
    {
        c = getchar();

        if (c == EOF || c == '\n')
        {
            buffer[position] = '\0';
            return;
        }
        else
        {
            buffer[position++] = c;
        }
    }
}

void tokenize(char **buffer, char *line)
{
    int position = 0;
    char *token = strtok(line, " ");

    while (token != NULL)
    {
        buffer[position++] = strdup(token);
        token = strtok(NULL, " ");
    }

    buffer[position] = NULL;
}

/* Find the active job with the indicated pgid.  */
job *find_job(pid_t pgid)
{
    job *j;

    for (j = first_job; j; j = j->next)
        if (j->pgid == pgid)
            return j;
    return NULL;
}

/* Return true if all processes in the job have stopped or completed.  */
int job_is_stopped(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        if (!p->completed && !p->stopped)
            return 0;
    return 1;
}

/* Return true if all processes in the job have completed.  */
int job_is_completed(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        if (!p->completed)
            return 0;
    return 1;
}

/* Make sure the shell is running interactively as the foreground job
   before proceeding. */
void init_shell()
{

    /* See if we are running interactively.  */
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive)
    {
        /* Loop until we are in the foreground.  */
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);

        /* Ignore interactive and job-control signals.  */
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGCHLD, SIG_IGN);

        /* Put ourselves in our own process group.  */
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0)
        {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }

        /* Grab control of the terminal.  */
        tcsetpgrp(shell_terminal, shell_pgid);

        /* Save default terminal attributes for shell.  */
        tcgetattr(shell_terminal, &shell_tmodes);
    }
}

void launch_process(process *p, pid_t pgid,
                    int infile, int outfile, int errfile,
                    int foreground)
{
    pid_t pid;

    if (shell_is_interactive)
    {
        /* Put the process into the process group and give the process group
           the terminal, if appropriate.
           This has to be done both by the shell and in the individual
           child processes because of potential race conditions.  */
        pid = getpid();
        if (pgid == 0)
            pgid = pid;
        setpgid(pid, pgid);
        if (foreground)
            tcsetpgrp(shell_terminal, pgid);

        /* Set the handling for job control signals back to the default.  */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
    }

    /* Set the standard input/output channels of the new process.  */
    if (infile != STDIN_FILENO)
    {
        dup2(infile, STDIN_FILENO);
        close(infile);
    }
    if (outfile != STDOUT_FILENO)
    {
        dup2(outfile, STDOUT_FILENO);
        close(outfile);
    }
    if (errfile != STDERR_FILENO)
    {
        dup2(errfile, STDERR_FILENO);
        close(errfile);
    }

    /* Exec the new process.  Make sure we exit.  */
    execvp(p->argv[0], p->argv);
    perror("execvp");
    exit(1);
}

int launch_job(job *j, int foreground)
{
    process *p;
    pid_t pid;
    int mypipe[2], infile, outfile;

    infile = j->stdin;
    for (p = j->first_process; p; p = p->next)
    {
        /* Set up pipes, if necessary.  */
        if (p->next)
        {
            if (pipe(mypipe) < 0)
            {
                perror("pipe");
                exit(1);
            }
            outfile = mypipe[1];
        }
        else
            outfile = j->stdout;

        /* Fork the child processes.  */
        pid = fork();
        if (pid == 0)
            /* This is the child process.  */
            launch_process(p, j->pgid, infile,
                           outfile, j->stderr, foreground);
        else if (pid < 0)
        {
            /* The fork failed.  */
            perror("fork");
            exit(1);
        }
        else
        {
            /* This is the parent process.  */
            p->pid = pid;
            if (shell_is_interactive)
            {
                if (!j->pgid)
                    j->pgid = pid;
                setpgid(pid, j->pgid);
            }
        }

        /* Clean up after pipes.  */
        if (infile != j->stdin)
            close(infile);
        if (outfile != j->stdout)
            close(outfile);
        infile = mypipe[0];
    }

    format_job_info(j, "launched");

    if (!shell_is_interactive)
        wait_for_job(j);
    else if (foreground)
        return put_job_in_foreground(j, 0);
    else
        put_job_in_background(j, 0);
}

/* Check the builtin commands. If not a builtin, launch the executable in PATH.
    Builtins return 1. Exit builtin returns 0. If no builtins are executed,
    return -1. */
int execute(job *j, int foreground)
{
    int i;

    if (j->first_process->argv[0] == NULL)
    {
        return 1;
    }

    for (i = 0; i < psh_num_builtins(); i++)
    {
        if (strcmp(j->first_process->argv[0], builtin_str[i]) == 0)
        {
            return (*(func_arr[i]))(j->first_process->argv);
        }
    }

    return -1;
}

/* Put job j in the foreground.  If cont is nonzero,
   restore the saved terminal modes and send the process group a
   SIGCONT signal to wake it up before we block.  */
int put_job_in_foreground(job *j, int cont)
{
    /* Put the job into the foreground.  */
    tcsetpgrp(shell_terminal, j->pgid);

    /* Send the job a continue signal, if necessary.  */
    if (cont)
    {
        tcsetattr(shell_terminal, TCSADRAIN, &j->tmodes);
        if (kill(-j->pgid, SIGCONT) < 0)
            perror("kill (SIGCONT)");
    }

    /* Wait for it to report.  */
    wait_for_job(j);
    int last_proc_status;
    process *p = j->first_process;
    do
    {
        last_proc_status = p->exit_status;
        p = p->next;
    } while (p);

    /* Put the shell back in the foreground.  */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Restore the shell’s terminal modes.  */
    tcgetattr(shell_terminal, &j->tmodes);
    tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);

    return last_proc_status;
}

/* Put a job in the background.  If the cont argument is true, send
   the process group a SIGCONT signal to wake it up.  */
void put_job_in_background(job *j, int cont)
{
    /* Send the job a continue signal, if necessary.  */
    if (cont)
        if (kill(-j->pgid, SIGCONT) < 0)
            perror("kill (SIGCONT)");
}

/* Store the status of the process pid that was returned by waitpid.
   Return 0 if all went well, nonzero otherwise.  */
int mark_process_status(pid_t pid, int status)
{
    job *j;
    process *p;

    if (pid > 0)
    {
        /* Update the record for the process.  */
        for (j = first_job; j; j = j->next)
            for (p = j->first_process; p; p = p->next)
                if (p->pid == pid)
                {
                    p->status = status;
                    if (WIFSTOPPED(status))
                    {
                        p->stopped = 1;
                    }
                    else
                    {
                        p->completed = 1;
                        if (WIFEXITED(status))
                        {
                            p->exit_status = WEXITSTATUS(status); // Store the exit code
                        }
                        else if (WIFSIGNALED(status))
                        {
                            p->exit_status = WTERMSIG(status); // Store the signal number
                            fprintf(stderr, "%d: Terminated by signal %d.\n",
                                    (int)pid, WTERMSIG(p->status));
                        }
                    }
                    return 0;
                }
        fprintf(stderr, "No child process %d.\n", pid);
        return -1;
    }
    else if (pid == 0 || errno == ECHILD)
        /* No processes ready to report.  */
        return -1;
    else
    {
        /* Other weird errors.  */
        perror("waitpid");
        return -1;
    }
}

/* Check for processes that have status information available,
   without blocking.  */
void update_status(void)
{
    int status;
    pid_t pid;

    do
        pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
    while (!mark_process_status(pid, status));
}

/* Check for processes that have status information available,
   blocking until all processes in the given job have reported.  */
void wait_for_job(job *j)
{
    int status;
    pid_t pid;

    do
    {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED);
    } while (!mark_process_status(pid, status) && !job_is_stopped(j) && !job_is_completed(j));
}

/* Format information about job status for the user to look at.  */
void format_job_info(job *j, const char *status)
{
    fprintf(stderr, "%ld (%s): %s\n", (long)j->pgid, status, j->command);
}

/* Notify the user about stopped or terminated jobs.
   Delete terminated jobs from the active job list.  */
void do_job_notification(void)
{
    job *j, *jlast, *jnext;

    /* Update status information for child processes.  */
    update_status();

    jlast = NULL;
    for (j = first_job; j; j = jnext)
    {
        jnext = j->next;

        /* If all processes have completed, tell the user the job has
           completed and delete it from the list of active jobs.  */
        if (job_is_completed(j))
        {
            format_job_info(j, "completed");
            if (jlast)
                jlast->next = jnext;
            else
                first_job = jnext;
            free_job(j);
        }

        /* Notify the user about stopped jobs,
           marking them so that we won’t do this more than once.  */
        else if (job_is_stopped(j) && !j->notified)
        {
            format_job_info(j, "stopped");
            j->notified = 1;
            jlast = j;
        }

        /* Don’t say anything about jobs that are still running.  */
        else
            jlast = j;
    }
}

/* Mark a stopped job J as being running again.  */
void mark_job_as_running(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        p->stopped = 0;
    j->notified = 0;
}

/* Continue the job J.  */
void continue_job(job *j, int foreground)
{
    mark_job_as_running(j);
    if (foreground)
        put_job_in_foreground(j, 1);
    else
        put_job_in_background(j, 1);
}

/* Free the job J. */
void free_job(job *j)
{
    process *p = j->first_process;
    while (p != NULL)
    {
        process *next = p->next;
        free(p->argv);
        free(p);
        p = next;
    }
    free(j->command);
    free(j);
}
