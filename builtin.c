#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "builtin.h"
#include "main.h"
#include <ctype.h>
#include "env.h"

extern job *first_job;
extern Env *first_env;

int psh_cd(char **args)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "psh: expected argument to \"cd\"\n");
    }
    else
    {
        if (chdir(args[1]) != 0)
        {
            perror("psh");
        }
    }
    return 1;
}

int psh_help(char **args)
{
    int i;
    printf("PSH\n");
    printf("Type program names and arguments, and hit enter.\n");
    printf("The following are built in:\n");
    for (i = 0; i < psh_num_builtins(); i++)
    {
        printf("  %s\n", builtin_str[i]);
    }
    printf("Use the \"man\" command for information on other programs.\n");
    return 1;
}

int psh_exit(char **args)
{
    // send sighup to every job.
    job *j2 = first_job;
    while (j2)
    {
        if (j2->pgid > 0 && killpg(j2->pgid, SIGHUP) < 0)
            perror("kill (SIGHUP)");
        j2 = j2->next;
    }

    return 0;
}

/* Find the last job that started running in the background. */
job *_find_last_bg_job()
{
    job *j = first_job;
    job *last_stopped_or_bg = NULL;

    while (j)
    {
        if (j->in_bg)
            last_stopped_or_bg = j;
        j = j->next;
    }
    return last_stopped_or_bg;
}

/* Find the last job that was stopped or started running in the background. */
job *_find_last_stopped_or_bg_job()
{
    job *j = first_job;
    job *last_stopped_or_bg = NULL;

    while (j)
    {
        if (job_is_stopped(j) || j->in_bg)
            last_stopped_or_bg = j;
        j = j->next;
    }
    return last_stopped_or_bg;
}

/* Find the last job that was stopped with Ctrl + Z or SIGTSTP. */
job *_find_last_stopped_job()
{
    job *j = first_job;
    job *last_stopped = NULL;

    while (j)
    {
        if (job_is_stopped(j))
            last_stopped = j;
        j = j->next;
    }
    return last_stopped;
}

int psh_jobs(char **args)
{
    int counter = 1;
    job *j = first_job;
    job *last_stopped = _find_last_stopped_job();
    char *stopped_or_running;
    char *plus_or_minus;
    while (j)
    {
        if (j->pgid != 0)
        {
            if (job_is_stopped(j))
                stopped_or_running = "stopped";
            else
                stopped_or_running = "running";
            if (last_stopped != NULL && j->pgid == last_stopped->pgid)
                plus_or_minus = "+";
            else if (last_stopped != NULL)
                plus_or_minus = "-";
            printf("[%d] %s %s %d %s\n", counter++, plus_or_minus, stopped_or_running, j->pgid, j->command);
        }
        j = j->next;
    }
    return 1;
}

/* Check if given string contains of only digits. If not, return -1.
   Else return the string converted to the integer type. */
int _check_if_str_is_valid(char *str)
{
    int i = 0;
    int isValid = 1;
    int num = -1;
    if (str[0] == '%')
        i = 1;
    while (str[i] != '\0')
    {
        if (!isdigit(str[i]))
        {
            isValid = 0;
            break;
        }
        i++;
    }

    if (isValid)
    {
        if (str[0] == '%')
            num = atoi(&str[1]);
        else
            num = atoi(str);
    }
    return num;
}

/* If a job is stopped and has the same index as the one provided, return it.
   Else return NULL. */
job *_find_job_by_index(int index)
{
    job *temp = first_job;
    int counter = 1;
    while (temp)
    {
        if (temp->pgid != 0)
        {
            if (counter == index)
            {
                // if (job_is_stopped(temp))
                //     return temp;
                // else
                //     return NULL;
                return temp;
            }
            counter++;
        }
    }
    return NULL;
}

int psh_fg(char **args)
{
    if (args[1] == NULL)
    {
        job *j = _find_last_stopped_or_bg_job();
        continue_job(j, 1, job_is_stopped(j));
        return 1;
    }
    int num;
    for (int i = 1; args[i] != NULL; i++)
    {
        num = _check_if_str_is_valid(args[i]);
        if (num == -1)
            continue;

        if (args[i][0] == '%')
        {
            job *j = _find_job_by_index(num);
            continue_job(j, 1, job_is_stopped(j)); // index
        }
        else
        {
            job *j = find_job(num);
            continue_job(j, 1, job_is_stopped(j)); // pid
        }
    }
    return 1;
}

int psh_bg(char **args)
{
    if (args[1] == NULL)
    {
        job *j = _find_last_stopped_or_bg_job();
        continue_job(j, 0, job_is_stopped(j));
        return 1;
    }
    int num;
    for (int i = 1; args[i] != NULL; i++)
    {
        num = _check_if_str_is_valid(args[i]);
        if (num == -1)
            continue;

        if (args[i][0] == '%')
        {
            job *j = _find_job_by_index(num);
            continue_job(j, 0, job_is_stopped(j)); // index
        }
        else
        {
            job *j = find_job(num);
            continue_job(j, 0, job_is_stopped(j)); // pid
        }
    }
    return 1;
}

int psh_source(char **argv)
{
    free_env_list();
    read_config_file();
    return 1;
}

int psh_set(char **argv)
{
    int elem_count = count_elem_in_list(argv);
    if (elem_count < 2)
    {
        fprintf(stderr, "Not enough arguments\n");
        return 1;
    }
    char **arr;
    for (int i = 1; argv[i] != NULL; i++)
    {
        if (containsChar(argv[i], '='))
        {
            arr = _split_string(argv[i], "=");
            if (!arr)
            {
                fprintf(stderr, "Argument must be of type NAME=VALUE, but was %s\n", argv[i]);
                break;
            }
            psh_setenv(arr[0], arr[1]);
        }
        else
        {
            fprintf(stderr, "Argument must be of type NAME=VALUE, but was %s\n", argv[i]);
            break;
        }
    }

    return 1;
}

int psh_unset(char **argv)
{
    int elem_count = count_elem_in_list(argv);
    if (elem_count < 2)
    {
        fprintf(stderr, "Not enough arguments\n");
        return 1;
    }
    for (int i = 1; argv[i] != NULL; i++)
        psh_unsetenv(argv[i]);
    return 1;
}

// Array of built-in command function pointers
builtin_func func_arr[] = {
    &psh_cd,
    &psh_help,
    &psh_exit,
    &psh_jobs,
    &psh_fg,
    &psh_bg,
    &psh_source,
    &psh_set,
    &psh_unset};

// Array of built-in command strings
char *builtin_str[] = {
    "cd",
    "help",
    "exit",
    "jobs",
    "fg",
    "bg",
    "source",
    "set",
    "unset"};

int psh_num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}