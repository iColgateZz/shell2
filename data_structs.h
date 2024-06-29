#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>

#ifndef DATA_STRUCTS_H
#define DATA_STRUCTS_H

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
    int foreground;            /* true if job should be started as a foreground one. */
} job;

#endif