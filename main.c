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
#include "main.h"
#include "env.h"
#include <ctype.h>
#include "custom_print.h"
#include "history.h"

#define BUF_SIZE 1024
#define TOK_BUF_SIZE 64

pid_t shell_pgid;
struct termios shell_tmodes, raw;
int shell_terminal;
int shell_is_interactive;
job *first_job = NULL;
int last_proc_exit_status, inverted;
Env *first_env = NULL;
History *last_history = NULL;
History *cur_history = NULL;

void init_line_editing();
void disable_raw_mode();
void free_tokens(char **tokens);
void free_wr_list(wrapper **list);

int main(void)
{
    char *line = malloc(BUF_SIZE * sizeof(char));
    char **tokens;
    wrapper **list;
    int status = 1;
    int check_status;
    int prompt_type = 0;

    if (!line)
    {
        my_fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    char *prompt1 = NULL;
    char *prompt2 = NULL;
    /* Make sure the shell is a foreground process. */
    init_shell();
    /* Read data from a configuration file. */
    read_config_file();
    /* Read data from the history file. */
    load_history();

    do
    {
        /* Regular shell cycle. */

        /* Updating the prompts for dir and branch changes.
           In case the prompt is configured via .pshrc file.  */
        if (prompt_type == 0){
            prompt1 = configure_prompt("PS1", prompt1);
            printf("%s", prompt1);
        } else {
            prompt2 = configure_prompt("PS2", prompt2);
            printf("%s", prompt2);
        }

        read_line(line);
        line = trim(line);
        /* Empty command check. */
        if (line[0] == '\0')
            continue;

        /* Check if the provided line can be parsed. */
        tokens = tokenize(line);
        if ((check_status = check_tokens(tokens)) == 0)
        {
            expand(tokens);       // perform various expansions
            add_to_history(line); // manage history
            cur_history = NULL;
            list = create_jobs(tokens);
            if (list == NULL)
            {
                free(list);
                continue;
            }
            status = launch_jobs(list);
            do_job_notification();
            prompt_type = 0;
            line[0] = '\0';

            free_wr_list(list);
            free_tokens(tokens);
        }
        else if (check_status == 1)
        { // Case when line continuation is needed.
            prompt_type = 1;
            free_tokens(tokens);
            continue;
        }
        else
        { // Syntax error occured. 
            line[0] = '\0';
            prompt_type = 0;
            free_tokens(tokens);
            continue;
        }
    } while (status);

    if (shell_is_interactive)
        disable_raw_mode();
    save_history();
    free_env_list();

    free(prompt1);
    free(prompt2);
    free(line);
    return 0;
}

/* Free the wrapper list. */
void free_wr_list(wrapper **list)
{
    for (int i = 0; list[i] != NULL; i++)
    {
        if (list[i]->type == OPERATOR)
            free(list[i]->oper);
        free(list[i]);
    }
    free(list);
}

/* Free the token list. */
void free_tokens(char **tokens)
{
    for (int i = 0; tokens[i] != NULL; i++)
    {
        free(tokens[i]);
    }
    free(tokens);
}

/* Restore original terminal modes. */
void disable_raw_mode()
{
    tcsetattr(shell_terminal, TCSAFLUSH, &shell_tmodes);
}

/* Make a copy of the terminal modes of the original shell. Make the copy raw 
   and set it as the current terminal mode. */
void enable_raw_mode()
{
    atexit(disable_raw_mode);
    raw = shell_tmodes;
    cfmakeraw(&raw);
    tcsetattr(shell_terminal, TCSAFLUSH, &raw);
}

/* SIGWINCH signal handler. */
void handle_sigwinch(int sig)
{
    // Handle window size change if needed
    my_printf("Resize signal caught!\n\r");
}

/* Enable raw mode and set up a SIGWINCH signal listener. */
void init_line_editing()
{
    enable_raw_mode();
    signal(SIGWINCH, handle_sigwinch);
}

/* Categorize the tokens for the later syntax check. */
int *categorize_tokens(char **tokens)
{
    int arr[TOK_BUF_SIZE];
    int pos = 0, first = 1;
    for (int i = 0; tokens[i] != NULL; i++)
    {
        switch (tokens[i][0])
        {
        case '!':
            if (first)
                arr[pos++] = INVERSION;
            else
                arr[pos++] = ARG;
            break;
        case '|':
            arr[pos++] = PIPE;
            first = 1;
            break;
        case '"':
            arr[pos++] = QUOTE;
            if (strcmp(tokens[i], "\"") == 0)
                i++;
            while (tokens[i] != NULL && !endsWith(tokens[i], '"'))
            {
                i++;
            }
            if (!tokens[i])
            {
                arr[pos] = END;
                return arr;
            }
            arr[pos++] = QUOTE_END;
            break;
        default:
            if (isRedirection(tokens[i]))
                arr[pos++] = REDIRECTION;
            else if (isOperator(tokens[i]))
            {
                arr[pos++] = OPER;
                first = 1;
            }
            else if (endsWith(tokens[i], '\\'))
                arr[pos++] = LINE_CONTINUATION;
            else if (strcmp(tokens[i], "&") == 0)
            {
                arr[pos++] = BG_OPER;
                first = 1;
            }
            else if (first)
            {
                arr[pos++] = CMD;
                first = 0;
            }
            else
                arr[pos++] = ARG;
            break;
        }
    }
    arr[pos] = END;

    return arr;
}

/* Check if the provided tokens make sense syntactically. 
   Return 1 if line continuation is needed, -1 if error occured, 0 - success. */
int check_tokens(char **tokens)
{
    int *arr = categorize_tokens(tokens);
    int first = 1;
    int last_token, next_token;
    for (int i = 0; arr[i] != END; i++)
    {
        next_token = arr[i + 1];
        if (first)
        {
            if (arr[i] == INVERSION)
            {
                if (next_token == CMD)
                {
                    last_token = INVERSION;
                    continue;
                }
                else if (next_token == LINE_CONTINUATION)
                    return 1;
                else
                {
                    my_perror("Wrong after '!'");
                    return -1;
                }
            }
            else if (arr[i] == CMD)
            {
                first = 0;
                last_token = CMD;
                continue;
            }
            else
            {
                my_perror("Wrong first word!");
                return -1;
            }
        }
        switch (arr[i])
        {
        case ARG:
            if (last_token == CMD ||
                last_token == ARG ||
                last_token == QUOTE_END)
            {
                if (next_token != CMD &&
                    next_token != INVERSION)
                {
                    last_token = ARG;
                    break;
                }
                else
                {
                    my_perror("Wrong after ARG!");
                    return -1;
                }
            }
            else if (last_token == REDIRECTION)
            {
                if (next_token == REDIRECTION ||
                    next_token == LINE_CONTINUATION ||
                    next_token == PIPE ||
                    next_token == END ||
                    next_token == OPER ||
                    next_token == BG_OPER)
                {
                    last_token = ARG;
                    break;
                }
                else
                {
                    my_perror("Wrong after ARG2!");
                    return -1;
                }
            }
            else
            {
                my_perror("Weird error 1!");
                return -1;
            }

        case PIPE:
            if (last_token == CMD ||
                last_token == ARG ||
                last_token == QUOTE_END)
            {
                if (next_token == CMD ||
                    next_token == INVERSION)
                {
                    first = 1;
                    last_token = PIPE;
                    break;
                }
                else if (next_token == END ||
                         next_token == LINE_CONTINUATION)
                    return 1;
                else
                {
                    my_perror("Wrong after PIPE!");
                    return -1;
                }
            }
            else
            {
                my_perror("Weird error 2!");
                return -1;
            }

        case REDIRECTION:
            if (last_token == CMD ||
                last_token == ARG ||
                last_token == QUOTE_END)
            {
                if (next_token == ARG ||
                    next_token == QUOTE_END)
                {
                    last_token = REDIRECTION;
                    break;
                }
                else if (next_token == END ||
                         next_token == LINE_CONTINUATION)
                    return 1;
                else
                {
                    my_perror("Wrong after REDIRECTION!");
                    return -1;
                }
            }
            else
            {
                my_perror("Weird error 3!");
                return -1;
            }

        case OPER:
            if (last_token == CMD ||
                last_token == ARG ||
                last_token == QUOTE_END)
            {
                if (next_token == CMD ||
                    next_token == INVERSION)
                {
                    first = 1;
                    last_token = OPER;
                    break;
                }
                else if (next_token == END ||
                         next_token == LINE_CONTINUATION)
                    return 1;
                else
                {
                    my_perror("Wrong after OPERATOR!");
                    return -1;
                }
            }
            else
            {
                my_perror("Weird error 4!");
                return -1;
            }

        case LINE_CONTINUATION:
            if (next_token == END)
                return 1;
            else
            {
                my_perror("Wrong after LINE_CONT!");
                return -1;
            }

        case QUOTE:
            if (last_token == CMD ||
                last_token == ARG ||
                last_token == QUOTE ||
                last_token == QUOTE_END)
            {
                if (next_token == QUOTE ||
                    next_token == QUOTE_END)
                {
                    last_token = QUOTE;
                    break;
                }
                else if (next_token == END ||
                         next_token == LINE_CONTINUATION)
                    return 1;
                else
                {
                    my_perror("Wrong after QUOTE!");
                    return -1;
                }
            }
            else
            {
                my_perror("Weird error 5!");
                return -1;
            }

        case QUOTE_END:
            if (last_token == CMD ||
                last_token == ARG ||
                last_token == QUOTE)
            {
                if (next_token != CMD &&
                    next_token != INVERSION)
                {
                    last_token = QUOTE_END;
                    break;
                }
                else
                {
                    my_perror("Wrong after QUOTE!");
                    return -1;
                }
            }
            else
            {
                my_perror("Weird error 6!");
                return -1;
            }

        case BG_OPER:
            if (last_token == CMD ||
                last_token == ARG ||
                last_token == QUOTE_END)
            {
                if (next_token == CMD ||
                    next_token == INVERSION ||
                    next_token == END)
                {
                    first = 1;
                    last_token = BG_OPER;
                    break;
                }
                else if (next_token == LINE_CONTINUATION)
                    return 1;
                else
                {
                    my_perror("Wrong after BG_OPER!");
                    return -1;
                }
            }
            else
            {
                my_perror("Weird error 7!");
                return -1;
            }
        }
    }
    return 0;
}

/* Function used for debugging. */
void print_list(wrapper **list)
{
    for (int i = 0; list[i] != NULL; i++)
    {
        if (list[i]->type == OPERATOR)
        {
            my_printf("Type OPERATOR\n");
            my_printf("Text %s\n", list[i]->oper);
        }
        else
        {
            my_printf("Type JOB\n");
            my_printf("Command %s\n", list[i]->j->command);
            my_printf("PGID %d\n", list[i]->j->pgid);
            my_printf("\tProcesses: \n");
            process *temp = list[i]->j->first_process;
            do
            {
                int k = 0;
                while (temp->argv[k] != NULL)
                {
                    my_printf("\tArg %s\n", temp->argv[k]);
                    k++;
                }
                temp = temp->next;
            } while (temp != NULL);
        }
    }
}

/* Launch jobs sequentially. Store the exit status of perfomed job. */
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
                inverted = list[i]->j->inverted;
                launch_job(list[i]->j, list[i]->j->foreground);
                if (inverted)
                    last_proc_exit_status = !last_proc_exit_status;
            }
        }
        else if (list[i - 1]->type == OPERATOR && list[i]->type == JOB)
        {
            if (strcmp(list[i - 1]->oper, ";") == 0)
            {
                inverted = list[i]->j->inverted;
                launch_job(list[i]->j, list[i]->j->foreground);
                if (inverted)
                    last_proc_exit_status = !last_proc_exit_status;
            }
            else if (strcmp(list[i - 1]->oper, "&") == 0)
            {
                launch_job(list[i]->j, list[i]->j->foreground);
                last_proc_exit_status = 0;
            }
            else if (strcmp(list[i - 1]->oper, "&&") == 0)
            {
                if (last_proc_exit_status == EXIT_SUCCESS)
                {
                    inverted = list[i]->j->inverted;
                    launch_job(list[i]->j, list[i]->j->foreground);
                    if (inverted)
                        last_proc_exit_status = !last_proc_exit_status;
                }
                else
                {
                    return 1;
                }
            }
            else
            {
                if (last_proc_exit_status != EXIT_SUCCESS)
                {
                    inverted = list[i]->j->inverted;
                    launch_job(list[i]->j, list[i]->j->foreground);
                    if (inverted)
                        last_proc_exit_status = !last_proc_exit_status;
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

/* Create a wrapper for a job. */
wrapper *create_job_wrapper(char **tokens, int start, int end)
{
    wrapper *wr = malloc(sizeof(wrapper));
    if (!wr)
    {
        my_fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    wr->type = JOB;
    wr->j = create_job(tokens, start, end);
    if (wr->j == NULL)
        return NULL;

    return wr;
}

/* Create a wrapper for an operator. */
wrapper *create_oper_wrapper(char *str)
{
    wrapper *wr2 = malloc(sizeof(wrapper));
    if (!wr2)
    {
        my_fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    wr2->type = OPERATOR;
    wr2->oper = strdup(str);

    return wr2;
}

/* Create a list of wrapper structs. */
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
        my_fprintf(stderr, "psh: allocation error\n");
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
        else if (strcmp(tokens[end], "&") == 0)
        {
            end++;
            wrapper *wr = create_job_wrapper(tokens, start, end);
            list[position++] = wr;

            wrapper *wr2 = create_oper_wrapper("&");
            list[position++] = wr2;
            start = end;
        }
        end++;
    }
    wrapper *wr = create_job_wrapper(tokens, start, end);
    if (wr != NULL)
        list[position++] = wr;

    list[position] = NULL;
    return list;
}

/* Create a job struct. */
job *create_job(char **tokens, int start, int end)
{
    if (tokens[start] == NULL)
        return NULL;
    int last_pipe_index = start;
    // Create a new job
    job *j = malloc(sizeof(job));
    if (!j)
    {
        my_fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    j->stdin = STDIN_FILENO;
    j->stdout = STDOUT_FILENO;
    j->stderr = STDERR_FILENO;
    j->pgid = 0, j->notified = 0, j->inverted = 0;
    j->first_process = NULL, j->next = NULL;
    if (first_job == NULL)
        first_job = j;
    else
    {
        job *temp = first_job;
        while (temp->next != NULL)
            temp = temp->next;
        temp->next = j;
    }

    if (strcmp(tokens[start], "!") == 0)
    {
        j->inverted = 1;
        start++;
        last_pipe_index++;
    }
    j->command = trim(concat_line(tokens, start, end));

    /* Create the processes. */
    for (int i = start; i < end; i++)
    {
        if (strcmp(tokens[i], "|") == 0 || tokens[i + 1] == NULL || i + 1 == end)
        {
            process *p = malloc(sizeof(process));
            if (!p)
            {
                my_fprintf(stderr, "psh: allocation error\n");
                exit(EXIT_FAILURE);
            }
            p->completed = 0, p->stopped = 0;
            p->next = NULL;
            p->argv = malloc(TOK_BUF_SIZE * sizeof(char *));
            if (!p->argv)
            {
                my_fprintf(stderr, "psh: allocation error\n");
                exit(EXIT_FAILURE);
            }
            p->infile = NULL, p->outfile = NULL, p->errfile = NULL;

            int position = 0;
            for (int j = last_pipe_index; j <= i; j++)
            {
                if (strcmp(tokens[j], "|") != 0)
                {
                    // for quoting
                    if (tokens[j][0] == '"')
                    {
                        size_t len = strlen(tokens[j]);
                        memmove(tokens[j], tokens[j] + 1, len - 1);
                        tokens[j][len - 2] = '\0';
                        p->argv[position++] = strdup(tokens[j]);
                    }
                    else if (isRedirection(tokens[j]))
                    {
                        if (strcmp(tokens[j], ">") == 0)
                        {
                            p->outfile = tokens[j + 1];
                            p->append_mode = 0;
                        }
                        else if (strcmp(tokens[j], ">>") == 0)
                        {
                            p->outfile = tokens[j + 1];
                            p->append_mode = 1;
                        }
                        else if (strcmp(tokens[j], "<") == 0)
                            p->infile = tokens[j + 1];
                        else if (strcmp(tokens[j], "2>") == 0)
                            p->errfile = tokens[j + 1];
                        j++;
                    }
                    else
                        p->argv[position++] = strdup(tokens[j]);
                }
            }
            last_pipe_index = i + 1;

            p->argv[position] = NULL;
            if (endsWith(p->argv[position - 1], '&'))
            {
                j->foreground = 0;
                j->in_bg = 1;
                size_t len = strlen(p->argv[position - 1]);
                if (len == 1)
                    p->argv[position - 1] = NULL;
                else
                {
                    p->argv[position - 1][len - 1] = ' ';
                    p->argv[position - 1] = trim(p->argv[position - 1]);
                }
            }
            else
                j->foreground = 1;

            if (j->first_process == NULL)
                j->first_process = p;
            else
            {
                process *proc = j->first_process;
                while (proc->next != NULL)
                    proc = proc->next;
                proc->next = p;
            }
        }
    }

    return j;
}

/* Read the line entered by the user. If the shell is used interactively,
   the terminal enters raw mode. Handle shortcuts, character insertion, and deletion. */
void read_line(char *buffer)
{
    int position = strlen(buffer);
    int cursor_pos = position;
    int c;

    memset(buffer + position, '\0', BUF_SIZE - position);

    if (position != 0)
    {
        if (buffer[position - 1] == '\\')
            buffer[position - 1] = ' ';
        else
            buffer[position++] = ' ';
    }

    while (1)
    {
        c = getchar();

        if (c == '\n' || c == '\r')
        {
            buffer[position] = '\0';
            my_printf("\n");
            return;
        }
        else if (c == 127)
        { // Handle backspace
            if (cursor_pos > 0)
            {
                memmove(&buffer[cursor_pos - 1], &buffer[cursor_pos], position - cursor_pos + 1);
                position--;
                cursor_pos--;
                printf("\b \b");
                printf("%s ", &buffer[cursor_pos]);
                for (int i = 0; i <= position - cursor_pos; i++)
                {
                    printf("\b");
                }
            }
        }
        else if (c == 21)
        { // Ctrl-U - delete from cursor to the start of the line 21
            while (cursor_pos > 0)
            {
                memmove(&buffer[cursor_pos - 1], &buffer[cursor_pos], position - cursor_pos + 1);
                position--;
                cursor_pos--;
                printf("\b \b");
                printf("%s ", &buffer[cursor_pos]);
                for (int i = 0; i <= position - cursor_pos; i++)
                {
                    printf("\b");
                }
            }
        }
        else if (c == 11)
        { // Ctrl-K - delete from cursor to the end of the line 11
            if (cursor_pos < position)
            {
                for (int i = cursor_pos; i < position; i++)
                {
                    printf(" ");
                }
                for (int i = cursor_pos; i < position; i++)
                {
                    printf("\b");
                }
                buffer[cursor_pos] = '\0';
                position = cursor_pos;
            }
        }
        else if (c == 1)
        { // Handle Ctrl-A (move to beginning)
            while (cursor_pos > 0)
            {
                printf("\b");
                cursor_pos--;
            }
        }
        else if (c == 5)
        { // Handle Ctrl-E (move to end)
            while (cursor_pos < position)
            {
                printf("%c", buffer[cursor_pos]);
                cursor_pos++;
            }
        }
        else if (c == 23)
        { // Handle Ctrl-W (delete word)
            if (cursor_pos > 0)
            {
                int prev_c_pos = cursor_pos;
                while (cursor_pos > 0 && buffer[cursor_pos - 1] == ' ')
                {
                    cursor_pos--;
                    printf("\b \b");
                }
                while (cursor_pos > 0 && buffer[cursor_pos - 1] != ' ')
                {
                    cursor_pos--;
                    printf("\b \b");
                }
                memmove(&buffer[cursor_pos], &buffer[prev_c_pos], position - prev_c_pos + 1);
                position -= (prev_c_pos - cursor_pos);

                for (int i = cursor_pos; i < position + (prev_c_pos - cursor_pos); i++)
                {
                    printf(" ");
                }
                for (int i = cursor_pos; i < position + (prev_c_pos - cursor_pos); i++)
                {
                    printf("\b");
                }
                printf("%s", &buffer[cursor_pos]);
                for (int i = cursor_pos; i < position; i++)
                {
                    printf("\b");
                }
            }
        }
        else if (c >= 32 && c <= 126)
        { // Printable characters
            memmove(&buffer[cursor_pos + 1], &buffer[cursor_pos], position - cursor_pos + 1);
            buffer[cursor_pos] = c;
            position++;
            cursor_pos++;
            printf("%s", &buffer[cursor_pos - 1]);
            for (int i = 0; i < position - cursor_pos; i++)
            {
                printf("\b");
            }
        }
        else if (c == 27) // Escape character
        {
            c = getchar();
            if (c == 91) // [
            {
                c = getchar();
                switch (c)
                {
                case 'A': // Up-Arrow
                    if (!cur_history && last_history)
                        cur_history = last_history;
                    else if (cur_history && cur_history->prev)
                        cur_history = cur_history->prev;
                    else
                        break;

                    while (cursor_pos < position)
                    {
                        printf("%c", buffer[cursor_pos]);
                        cursor_pos++;
                    }
                    while (cursor_pos > 0)
                    {
                        printf("\b \b");
                        cursor_pos--;
                    }
                    memset(buffer, '\0', position);
                    position = 0;

                    strcpy(buffer, cur_history->line);
                    printf("%s", buffer);

                    position = strlen(buffer);
                    cursor_pos = position;
                    break;
                case 'B': // Down-Arrow
                    if (cur_history && cur_history->next)
                        cur_history = cur_history->next;
                    else if (cur_history && !cur_history->next)
                        cur_history = NULL;
                    else
                        break;
                    while (cursor_pos < position)
                    {
                        printf("%c", buffer[cursor_pos]);
                        cursor_pos++;
                    }
                    while (cursor_pos > 0)
                    {
                        printf("\b \b");
                        cursor_pos--;
                    }
                    memset(buffer, '\0', position);
                    position = 0;

                    if (!cur_history)
                        strcpy(buffer, "");
                    else
                        strcpy(buffer, cur_history->line);
                    printf("%s", buffer);

                    position = strlen(buffer);
                    cursor_pos = position;
                    break;
                case 'C':
                    if (cursor_pos < position)
                    {
                        printf("%c", buffer[cursor_pos]);
                        cursor_pos++;
                    }
                    break;
                case 'D':
                    if (cursor_pos > 0)
                    {
                        printf("\b");
                        cursor_pos--;
                    }
                    break;
                }
            }
        }
        // Ctrl-L - clear screen 12
    }
}

/* Tokenize the provided string. */
char **tokenize(char *line)
{
    int position = 0;
    int start = 0;
    int in_quotes = 0;
    int len = strlen(line);
    char *token;
    char **buffer = malloc(TOK_BUF_SIZE * sizeof(char *));
    if (!buffer)
    {
        my_fprintf(stderr, "psh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    

    for (int i = 0; i <= len; i++)
    {
        if (line[i] == '"')
        {
            in_quotes = !in_quotes;
        }
        else if (isspace(line[i]) && !in_quotes)
        {
            if (i > start)
            {
                token = strndup(line + start, i - start);
                buffer[position++] = token;
            }
            start = i + 1;
        }
        else if (line[i] == '\0')
        {
            if (i > start)
            {
                token = strndup(line + start, i - start);
                buffer[position++] = token;
            }
        }
    }
    
    buffer[position] = NULL;
    return buffer;
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
    if (!j)
        return -1;
    process *p;
    for (p = j->first_process; p; p = p->next)
        if (!p->completed && !p->stopped)
        {
            return 0;
        }
    return 1;
}

/* Return true if all processes in the job have completed.  */
int job_is_completed(job *j)
{
    if (!j)
        return -1;
    process *p;
    for (p = j->first_process; p; p = p->next)
        if (!p->completed)
        {
            return 0;
        }
    return 1;
}

/* Make sure the shell is running interactively as the foreground job
   before proceeding. */
void init_shell()
{
    /* Check for non-interactive mode. Used for tests. */
    if (getenv("PSH_NON_INTERACTIVE"))
    {
        shell_is_interactive = 0;
        return;
    }

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

        /* Put ourselves in our own process group.  */
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0)
        {
            my_perror("Couldn't put the shell in its own process group");
            exit(1);
        }

        /* Grab control of the terminal.  */
        tcsetpgrp(shell_terminal, shell_pgid);

        /* Save default terminal attributes for shell.  */
        tcgetattr(shell_terminal, &shell_tmodes);

        /* Allow line editing. */
        init_line_editing();
    }
}

/* Launch the provided process P. */
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
        signal(SIGHUP, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
    }

    // Handle input redirection
    if (p->infile)
    {
        infile = open(p->infile, O_RDONLY);
        if (infile < 0)
        {
            my_perror("open input file");
            exit(1);
        }
    }

    // Handle output redirection
    if (p->outfile)
    {
        if (p->append_mode)
            outfile = open(p->outfile, O_WRONLY | O_CREAT | O_APPEND, 0644);
        else
            outfile = open(p->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outfile < 0)
        {
            my_perror("open output file");
            exit(1);
        }
    }

    // Handle error redirection
    if (p->errfile)
    {
        errfile = open(p->errfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errfile < 0)
        {
            my_perror("open error file");
            exit(1);
        }
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
    my_perror("execvp");
    exit(1);
}

/* Launch the job J. */
void launch_job(job *j, int foreground)
{
    process *p;
    pid_t pid;
    int mypipe[2], infile, outfile;
    char *prev_proc_outfile = NULL;

    infile = j->stdin;
    for (p = j->first_process; p; p = p->next)
    {
        if (prev_proc_outfile)
        {
            free(prev_proc_outfile);
            prev_proc_outfile = NULL;
        }
        if (p->outfile)
            prev_proc_outfile = strdup(p->outfile);

        /* Set up pipes, if necessary.  */
        if (p->next)
        {
            if (pipe(mypipe) < 0)
            {
                my_perror("pipe");
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
            my_perror("fork");
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

        /* Reassign the infile in case previous process had a non-default outfile. */
        if (prev_proc_outfile)
        {
            close(infile);
            infile = open(prev_proc_outfile, O_RDONLY);
        }
    }
    if (prev_proc_outfile)
        free(prev_proc_outfile);

    format_job_info(j, "launched");

    if (!shell_is_interactive)
        wait_for_job(j);
    else if (foreground)
        put_job_in_foreground(j, 0);
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
void put_job_in_foreground(job *j, int cont)
{
    j->in_bg = 0;
    /* Put the job into the foreground.  */
    tcsetpgrp(shell_terminal, j->pgid);

    /* Foreground jobs should be launched in non-raw mode.
       Otherwise they cause many line formatting issues. */
    disable_raw_mode();

    /* Send the job a continue signal, if necessary.  */
    if (cont)
    {
        tcsetattr(shell_terminal, TCSADRAIN, &j->tmodes);
        if (kill(-j->pgid, SIGCONT) < 0)
            my_perror("kill (SIGCONT)");
    }

    /* Wait for it to report.  */
    wait_for_job(j);

    /* Put the shell back in the foreground.  */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Restore the shell’s terminal modes.  */
    tcgetattr(shell_terminal, &j->tmodes);
    tcsetattr(shell_terminal, TCSADRAIN, &raw);
}

/* Put a job in the background.  If the cont argument is true, send
   the process group a SIGCONT signal to wake it up.  */
void put_job_in_background(job *j, int cont)
{
    /* Send the job a continue signal, if necessary.  */
    if (cont)
        if (kill(-j->pgid, SIGCONT) < 0)
            my_perror("kill (SIGCONT)");
    j->in_bg = 1;
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
                            last_proc_exit_status = p->exit_status;
                        }
                        else if (WIFSIGNALED(status))
                        {
                            p->exit_status = WTERMSIG(status); // Store the signal number
                            my_fprintf(stderr, "%d: Terminated by signal %d.\n",
                                       (int)pid, WTERMSIG(p->status));
                            last_proc_exit_status = p->exit_status;
                        }
                    }
                    return 0;
                }
        my_fprintf(stderr, "No child process %d.\n", pid);
        return -1;
    }
    else if (pid == 0 || errno == ECHILD)
        /* No processes ready to report.  */
        return -1;
    else
    {
        /* Other weird errors.  */
        my_perror("waitpid");
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
    {
        pid = waitpid(WAIT_ANY, &status, WUNTRACED | WNOHANG);
    } while (!mark_process_status(pid, status));
}

/* Check for processes that have status information available,
   blocking until all processes in the given job have reported.  */
void wait_for_job(job *j)
{
    int status;
    pid_t pid;

    do
    {
        pid = waitpid(-j->pgid, &status, WUNTRACED);
    } while (!mark_process_status(pid, status) && !job_is_stopped(j) && !job_is_completed(j));
}

/* Format information about job status for the user to look at.  */
void format_job_info(job *j, const char *status)
{
    // my_fprintf(stderr, "%ld (%s): %s\n", (long)j->pgid, status, j->command);
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

        else if (j->pgid == 0)
        {
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
void continue_job(job *j, int foreground, int send_cont)
{
    if (j == NULL)
        return;
    if (send_cont == -1)
        return;
    mark_job_as_running(j);
    if (foreground)
        put_job_in_foreground(j, send_cont);
    else
        put_job_in_background(j, send_cont);
}

/* Free the job J. */
void free_job(job *j)
{
    process *p = j->first_process;
    while (p != NULL)
    {
        process *next = p->next;

        // Free each argument string in argv
        if (p->argv)
        {
            for (char **arg = p->argv; *arg != NULL; ++arg)
            {
                free(*arg);
            }
            free(p->argv);
        }

        // Free the process structure itself
        free(p);

        p = next;
    }

    // Free the command string and the job structure itself
    if (j->command)
        free(j->command);
    free(j);
}
