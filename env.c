#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "helpers.h"

#define LINE_LEN 256
#define MAX_PROMPT_LEN 64

extern Env *first_env;

/* Get the value of the environmental variable corresponding to the given name.
   Return null pointer if there is no variable with such name. */
char *psh_getenv(char *name)
{
    char *value = NULL;
    Env *temp = first_env;
    if (!temp)
        return NULL;
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
    Env *prev, *next;
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
    prev->next = next;

    free(temp->name);
    free(temp->value);
    free(temp->next);
    free(temp);
}

char **_split_string(char *str, char *c)
{
    char *arr[2];
    arr[0] = trim(strtok(str, c));
    arr[1] = trim(strtok(NULL, c));

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