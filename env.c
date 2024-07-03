#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "helpers.h"
#include "main.h"
#include "builtin.h"
#include <ctype.h>
#include <glob.h>
#include "custom_print.h"

#define LINE_LEN 256
#define MAX_PROMPT_LEN 64
#define CONFIG_FILE ".pshrc"

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
        free(temp->value);
        temp->value = strdup(value);
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
    char **arr = malloc(2 * sizeof(char *));
    if (!arr)
        return NULL;

    char *token = strtok(str, c);
    if (!token)
    {
        free(arr);
        return NULL;
    }
    arr[0] = strdup(trim(token));
    token = strtok(NULL, c);
    if (!token)
    {
        free(arr[0]);
        free(arr);
        return NULL;
    }
    arr[1] = strdup(trim(token));

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
    char *filename = CONFIG_FILE;
    char line[LINE_LEN];
    FILE *file;
    char **arr;

    file = fopen(filename, "r");
    if (file == NULL)
    {
        my_perror("Error opening file");
        exit(1);
    }

    while (fgets(line, LINE_LEN, file))
    {
        if (line[0] != '#' && strchr(line, '='))
        {
            arr = _split_string(line, "=");
            if (arr)
            {
                psh_setenv(arr[0], arr[1]);
                free(arr[0]);
                free(arr[1]);
                free(arr);
            }
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
        my_fprintf(stderr, "Failed to run command\n");
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
        my_perror("Memory allocation error");
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
        my_fprintf(stderr, "Failed to run command\n");
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
        my_perror("Memory allocation error");
        exit(1);
    }

    return result;
}

char *_parse_ps_var(char *var)
{
    char *temp = malloc(MAX_PROMPT_LEN * sizeof(char));
    if (!temp)
    {
        my_perror("psh: allocation error");
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
                my_fprintf(stderr, "Unexpected token after '-': %c\n", var[i]);
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
        return;
    }

    for (int i = 1; i <= strlen(str); i++)
    {
        str[i - 1] = str[i];
    }
}

int _is_dollar_expandable(char *token)
{
    int $_index = -1;
    for (int i = 0; token[i] != '\0'; i++)
    {
        if (token[i] == '$')
        {
            $_index = i;
            break;
        }
    }
    if ($_index == -1)
        return 0;
    if (token[$_index + 1] == '\0' || token[$_index + 1] == '{')
        return 0;

    // printf("Got out of the dollar_expandable thingy\n");
    return 1;
}

char arr[] = {
    '$',
    '{',
    '[',
    '(',
    '\\',
    '/',
    '*',
    '?',
    '&',
    '|',
    '!',
    '~',
    '<',
    '>',
    '%',
    ':',
    ';',
    '"',
    '\'',
    '\0'};

int is_special_symbol(char c)
{
    for (int i = 0; arr[i] != '\0'; i++)
    {
        if (arr[i] == c)
            return 1;
    }
    return 0;
}

void _handle_dollar_expansion(char **tokens, char *token, int index)
{
    int prefix_len = 0, content_len = 0, suffix_len = 0;
    char *prefix = NULL, *content = NULL, *suffix = NULL;
    int first = 0;

    for (int i = 0; token[i] != '\0'; i++)
    {
        if (!first && token[i] == '$')
        {
            first = 1;
            prefix_len = i;
        }
        else if (first && is_special_symbol(token[i]))
        {
            // printf("Special symbol is %c\n", token[i]);
            content_len = i - prefix_len - 1;
            if (token[i] == '$' && content_len == 0)
                content_len++;
            else if (token[i] == '?' && content_len == 0)
                content_len++;
            else if (token[i] == '!' && content_len == 0)
                content_len++;
            break;
        }
        else if (first && token[i + 1] == '\0')
        {
            content_len = i - prefix_len;
            break;
        }
    }
    suffix_len = strlen(token) - prefix_len - content_len - 1;

    prefix = (char *)malloc(prefix_len + 1);
    content = (char *)malloc(content_len + 1);
    suffix = (char *)malloc(2 * suffix_len + 1); // It sometimes behaved weirdly causing a trace error

    if (!prefix || !content || !suffix)
    {
        free(prefix);
        free(content);
        free(suffix);
        return;
    }

    // printf("Prefix_len %d\n", prefix_len);
    // printf("Content_len %d\n", content_len);
    // printf("Suffix_len %d\n", suffix_len);

    strlcpy(prefix, token, prefix_len + 1);
    strlcpy(content, token + prefix_len + 1, content_len + 1);
    strlcpy(suffix, token + prefix_len + content_len + 1, suffix_len + 1);

    // printf("Prefix %s\n", prefix);
    // printf("Content %s\n", content);
    // printf("Suffix %s\n", suffix);

    char *expanded_content = NULL;
    if (strcmp(content, "?") == 0)
    {
        expanded_content = (char *)malloc(12);
        sprintf(expanded_content, "%d", last_proc_exit_status);
    }
    else if (strcmp(content, "$") == 0)
    {
        expanded_content = (char *)malloc(12);
        sprintf(expanded_content, "%d", shell_pgid);
    }
    else if (strcmp(content, "!") == 0)
    {
        pid_t pgid;
        job *j = _find_last_bg_job();
        if (!j)
            pgid = 0;
        else
            pgid = j->pgid;

        expanded_content = (char *)malloc(12);
        sprintf(expanded_content, "%d", pgid);
    }
    else
    {
        char *temp = psh_getenv(content);
        if (!temp)
            expanded_content = strdup("");
        else
            expanded_content = strdup(temp);
    }

    if (!expanded_content)
    {
        free(prefix);
        free(content);
        free(suffix);
        return;
    }

    size_t new_token_len = prefix_len + strlen(expanded_content) + suffix_len;
    char *new_token = (char *)malloc(new_token_len + 1);

    if (!new_token)
    {
        free(prefix);
        free(content);
        free(suffix);
        free(expanded_content);
        return;
    }

    sprintf(new_token, "%s%s%s", prefix, expanded_content, suffix);

    free(prefix);
    free(content);
    free(suffix);
    free(expanded_content);

    // my_printf("New_token %s\n", new_token);
    // my_printf("Tokens[index] %s\n", tokens[index]);
    // my_printf("Token %s\n", token);
    // my_printf("Tokens[index] pointer %p\n", tokens[index]);
    // my_printf("Token pointer %p\n", token);

    free(tokens[index]);
    tokens[index] = new_token;
    // printf("END of handle_dollar\n");
}

void _handle_wave(char **tokens, char *token, int index)
{
    char *home = strdup(getenv("HOME"));
    remove_first_char(token);
    strcat(home, token);
    free(tokens[index]);
    tokens[index] = home;
    // printf("New var is %s\n", tokens[index]);
}

int _find_curly_brace_expansion(const char *token)
{
    int open_i = -1, close_i = -1;
    for (int i = 0; token[i] != '\0'; i++)
    {
        if (token[i] == '{')
            open_i = i;
        else if (token[i] == '}')
        {
            close_i = i;
            break;
        }
    }
    if (open_i == -1 || close_i == -1 || open_i >= close_i || (close_i - open_i) <= 1)
        return 0;

    char *content = strndup(token + open_i + 1, close_i - open_i - 1);
    if (!content)
        return 0;

    // printf("content is %s\n", content);
    int is_comma_separated = 1;
    if (!strchr(content, ','))
        is_comma_separated = 0;
    for (int i = 0; content[i] != '\0'; i++)
    {
        if (!isalnum(content[i]) && content[i] != ',')
        {
            is_comma_separated = 0;
            break;
        }
    }
    // printf("is comma sep %d\n", is_comma_separated);
    int is_number_range = 0;
    char *dotdot = strstr(content, "..");
    if (dotdot)
    {
        char *first_part = strndup(content, dotdot - content);
        char *second_part = strdup(dotdot + 2);
        if (first_part && second_part)
        {
            is_number_range = 1;
            for (int i = 0; first_part[i] != '\0'; i++)
            {
                if (!isdigit(first_part[i]))
                {
                    is_number_range = 0;
                    break;
                }
            }
            for (int i = 0; second_part[i] != '\0'; i++)
            {
                if (!isdigit(second_part[i]))
                {
                    is_number_range = 0;
                    break;
                }
            }
        }
        free(first_part);
        free(second_part);
    }
    // printf("is .. sep %d\n", is_number_range);
    // sleep(2);
    free(content);

    return is_comma_separated || is_number_range;
}

void _handle_curly_brace_expansion(char **tokens, char *token, int index)
{
    char *start = strchr(token, '{');
    char *end = strchr(token, '}');

    if (start == NULL || end == NULL || start > end)
        return;

    char prefix[256] = {0};
    char suffix[256] = {0};
    char content[256] = {0};

    strncpy(prefix, token, start - token);
    strncpy(suffix, end + 1, strlen(end + 1));
    strncpy(content, start + 1, end - start - 1);

    // printf("Prefix is %s\n", prefix);
    // printf("Conten is %s\n", content);
    // printf("Suffix is %s\n", suffix);

    char **new_tokens = NULL;
    int new_token_count = 0;

    if (strchr(content, ','))
    {
        // Handle comma-separated list
        char *part = strtok(content, ",");
        while (part != NULL)
        {
            new_tokens = realloc(new_tokens, sizeof(char *) * (new_token_count + 1));
            new_tokens[new_token_count] = malloc(strlen(prefix) + strlen(part) + strlen(suffix) + 1);
            sprintf(new_tokens[new_token_count], "%s%s%s", prefix, part, suffix);
            new_token_count++;
            part = strtok(NULL, ",");
        }
    }
    else if (strchr(content, '.') && strchr(content, '.') != strrchr(content, '.'))
    {
        // Handle numeric range
        int start_num, end_num;
        if (sscanf(content, "%d..%d", &start_num, &end_num) == 2)
        {
            int k = start_num < end_num ? 1 : -1;
            for (int i = start_num; (k == 1) ? (i <= end_num) : (i >= end_num); i += k)
            {
                new_tokens = realloc(new_tokens, sizeof(char *) * (new_token_count + 1));
                new_tokens[new_token_count] = malloc(strlen(prefix) + 21 + strlen(suffix)); // 21 for integer
                sprintf(new_tokens[new_token_count], "%s%d%s", prefix, i, suffix);
                new_token_count++;
            }
        }
    }

    if (new_tokens != NULL)
    {
        int original_count = 0;
        while (tokens[original_count] != NULL)
            original_count++;

        tokens = realloc(tokens, sizeof(char *) * (original_count + new_token_count));
        memmove(&tokens[index + new_token_count], &tokens[index + 1], sizeof(char *) * (original_count - index));

        for (int i = 0; i < new_token_count; i++)
        {
            tokens[index + i] = new_tokens[i];
        }

        free(new_tokens);
    }
    free(token);
}

int _is_glob_expandable(char *str)
{
    if (str[0] == '"' && endsWith(str, '"'))
        return 0;
    for (int i = 0; str[i] != '\0'; i++)
    {
        if (str[i] == '*' || str[i] == '?')
            return 1;
    }
    return 0;
}

void _handle_glob_expansion(char **tokens, char *token, int index)
{
    glob_t glob_result;
    int ret = glob(token, GLOB_TILDE, NULL, &glob_result);
    if (ret != 0)
    {
        globfree(&glob_result);
        my_fprintf(stderr, "Glob error: %d\n", ret);
        return;
    }

    int num_matches = glob_result.gl_pathc;
    int original_count = 0;
    while (tokens[original_count] != NULL)
        original_count++;

    tokens = realloc(tokens, sizeof(char *) * (original_count + num_matches));
    if (!tokens)
    {
        globfree(&glob_result);
        my_fprintf(stderr, "Memory allocation error\n");
        return;
    }

    memmove(&tokens[index + num_matches], &tokens[index + 1], sizeof(char *) * (original_count - index));

    for (int i = 0; i < num_matches; i++)
    {
        tokens[index + i] = strdup(glob_result.gl_pathv[i]);
        if (!tokens[index + i])
        {
            for (int j = 0; j < index + i; j++)
            {
                free(tokens[index + j]);
            }
            globfree(&glob_result);
            my_fprintf(stderr, "Memory allocation error\n");
            return;
        }
    }

    globfree(&glob_result);
    free(token);
}

void expand(char **tokens)
{
    for (int i = 0; tokens[i] != NULL; i++)
    {
        if (tokens[i][0] == '~')
            _handle_wave(tokens, tokens[i], i);
        while (_is_dollar_expandable(tokens[i]))
            _handle_dollar_expansion(tokens, tokens[i], i);
        while (_find_curly_brace_expansion(tokens[i]))
            _handle_curly_brace_expansion(tokens, tokens[i], i);
        if (_is_glob_expandable(tokens[i]))
            _handle_glob_expansion(tokens, tokens[i], i);
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