#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "helpers.h"
#include "main.h"
#include "builtin.h"

#define LINE_LEN 256
#define MAX_PROMPT_LEN 64

extern Env *first_env;
extern int last_proc_exit_status;
extern pid_t shell_pgid;

/* Get the value of the environmental variable corresponding to the given name.
   Return null pointer if there is no variable with such name. */
char *psh_getenv(char *name)
{
    char *value = NULL;
    Env *temp = first_env;
    if (!temp)
        return getenv(name);
    do
    {
        if (strcmp(temp->name, name) == 0)
        {
            value = temp->value;
            break;
        }
        temp = temp->next;
    } while (temp);
    if (value)
        return value;
    else
        return getenv(name);
}

/* Add a variable to the list of environmental variables. Override existing value if
   a variable with such name already exists. */
void psh_setenv(char *name, char *value)
{
    Env *temp = first_env;
    Env *last_env;
    int found = 0;

    if (!temp)
    {
        Env *new = malloc(sizeof(Env));
        new->name = strdup(name);
        new->value = strdup(value);
        new->next = NULL;
        first_env = new;
        return;
    }

    do
    {
        if (strcmp(temp->name, name) == 0)
        {
            found = 1;
            break;
        }
        last_env = temp;
        temp = temp->next;
    } while (temp);

    if (found)
    {
        free(last_env->value);
        last_env->value = strdup(value);
    }
    else
    {
        Env *new = malloc(sizeof(Env));
        new->name = strdup(name);
        new->value = strdup(value);
        new->next = NULL;
        last_env->next = new;
    }
}

/* Unset the variable with the given name. Do nothing if no variable with such name exists. */
void psh_unsetenv(char *name)
{
    Env *temp = first_env;
    if (!temp)
        return;
    Env *prev = NULL, *next;
    int found = 0;
    do
    {
        if (strcmp(temp->name, name) == 0)
        {
            found = 1;
            next = temp->next;
            break;
        }
        prev = temp;
        temp = temp->next;
    } while (temp);
    if (!found)
        return;
    if (!prev)
        first_env = next;
    else
        prev->next = next;
    free(temp->name);
    free(temp->value);
    free(temp);
}

char **_split_string(char *str, char *c)
{
    char *arr[2];
    char *token = strtok(str, c);
    if (!token)
        return NULL;
    arr[0] = trim(token);
    token = strtok(NULL, c);
    if (!token)
        return NULL;
    arr[1] = trim(token);

    if (arr[1][0] == '"' && arr[1][strlen(arr[1]) - 1] == '"')
    {
        size_t len = strlen(arr[1]);
        memmove(arr[1], arr[1] + 1, len - 1);
        arr[1][len - 2] = '\0';
    }

    return arr;
}

/* Reads the configuration file and sets the environmental variables accordingly. */
void read_config_file()
{
    char *filename = ".pshrc";
    char line[LINE_LEN];
    FILE *file;
    char **arr;

    file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Error opening file");
        exit(1);
    }

    while (fgets(line, LINE_LEN, file))
    {
        if (line[0] != '#' && strchr(line, '='))
        {
            arr = _split_string(line, "=");
            psh_setenv(arr[0], arr[1]);
        }
    }

    fclose(file);
}

char *_get_current_dir()
{
    FILE *fp;
    char path[MAX_PROMPT_LEN];

    fp = popen("pwd", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Failed to run command\n");
        exit(1);
    }

    if (fgets(path, sizeof(path), fp) != NULL)
    {
        path[strcspn(path, "\n")] = '\0';
    }
    pclose(fp);

    char *temp = strtok(path, "/");
    char *last = temp;
    while (temp)
    {
        last = temp;
        temp = strtok(NULL, "/");
    }

    char *result = strdup(last);
    if (!result)
    {
        perror("Memory allocation error");
        exit(1);
    }

    return result;
}

char *_get_current_git_branch()
{
    FILE *fp;
    char path[MAX_PROMPT_LEN];

    fp = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Failed to run command\n");
        exit(1);
    }

    if (fgets(path, sizeof(path), fp) != NULL)
    {
        path[strcspn(path, "\n")] = '\0';
    }
    else
        path[0] = '\0';

    pclose(fp);

    char *result = strdup(path);
    if (!result)
    {
        perror("Memory allocation error");
        exit(1);
    }

    return result;
}

char *_parse_ps_var(char *var)
{
    char *temp = malloc(MAX_PROMPT_LEN * sizeof(char));
    if (!temp)
    {
        perror("psh: allocation error");
        exit(1);
    }
    int counter = 0;
    for (int i = 0; var[i] != '\0'; i++)
    {
        if (var[i] != '-')
        {
            temp[counter++] = var[i];
        }
        else
        {
            i++;
            if (var[i] == '\0')
                break;
            switch (var[i])
            {
            case 'b':
            {
                char *branch = _get_current_git_branch();
                for (int k = 0; branch[k] != '\0'; k++)
                    temp[counter++] = branch[k];
                free(branch);
                break;
            }
            case 'p':
            {
                char *cur_dir = _get_current_dir();
                for (int k = 0; cur_dir[k] != '\0'; k++)
                    temp[counter++] = cur_dir[k];
                free(cur_dir);
                break;
            }
            default:
                fprintf(stderr, "Unexpected token after '-': %c\n", var[i]);
                break;
            }
        }
    }
    temp[counter] = '\0';
    free(var);
    return temp;
}

char *configure_prompt(char *env)
{
    char *temp = psh_getenv(env);
    char *def = "$ ";
    char *def2 = "> ";
    if (!temp && strcmp(env, "PS1") == 0)
        return def;
    else if (!temp && strcmp(env, "PS2") == 0)
        return def2;

    char *var = strdup(temp);

    return _parse_ps_var(var);
}

void remove_first_char(char *str)
{
    if (str == NULL || strlen(str) == 0)
    {
        return; // If the string is NULL or empty, do nothing
    }

    // Shift all characters one position to the left
    for (int i = 1; i <= strlen(str); i++)
    {
        str[i - 1] = str[i];
    }
}

char *handle_$(char *token)
{
    remove_first_char(token);

    if (strcmp(token, "?") == 0)
        sprintf(token, "%d", last_proc_exit_status);
    else if (strcmp(token, "$") == 0)
        sprintf(token, "%d", shell_pgid);
    else if (strcmp(token, "!") == 0)
    {
        pid_t pgid;
        job *j = _find_last_bg_job();
        if (!j)
            pgid = 0;
        else
            pgid = j->pgid;

        sprintf(token, "%d", pgid);
    }
    else
    {
        char *temp = psh_getenv(token);
        if (!temp)
            token = '\0';
        else
            token = strdup(temp);
    }
    return token;
}

char *handle_wave(char *token)
{
    char *home = strdup(getenv("HOME"));
    remove_first_char(token);
    strcat(home, token);
    return home;
}

void expand(char **tokens)
{
    for (int i = 0; tokens[i] != NULL; i++)
    {
        if (tokens[i][0] == '$' && strlen(tokens[i]) > 1)
            tokens[i] = handle_$(tokens[i]);
        else if (tokens[i][0] == '~')
            tokens[i] = handle_wave(tokens[i]);
    }
}

void free_env_list()
{
    Env *temp = first_env;
    Env *next;
    if (!temp)
        return;
    
    do
    {
        next = temp->next;
        free(temp->name);
        free(temp->value);
        free(temp);
        temp = next;
    } while (temp);
    first_env = NULL;
}