#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "custom_print.h"
#include "helpers.h"
#include <ctype.h>
#include <glob.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

#define TOK_BUF_SIZE 256

extern int tab_count;
char *token_to_complete = NULL;
int word_start = -1;
char **possible_completions = NULL;
int real_tok_category = 0;

void free_token_to_complete()
{
    if (token_to_complete)
        free(token_to_complete);
}

void free_possible_completions()
{
    free_tokens(possible_completions);
}

char *append_star(char *str)
{
    size_t len = strlen(str);
    char *new_str = realloc(str, len + 2);
    if (new_str == NULL)
    {
        my_printf("Memory allocation failed\n");
        return str;
    }
    new_str[len] = '*';
    new_str[len + 1] = '\0';
    return new_str;
}

int cursor_on_token_with_index(char *buffer, int *position, int *cursor_pos, int *tok_start, int *tok_end)
{
    int tok_counter = -1;
    int start = 0, end = 0;
    for (int i = 0; i <= *position; i++)
    {
        if (isspace(buffer[i]))
        {
            end = i;
            if (start != end)
            {
                tok_counter++;
                if (start <= *cursor_pos && *cursor_pos <= end)
                {
                    *tok_start = start;
                    *tok_end = end;
                    return tok_counter;
                }
            }
            else if (start == end && i == *cursor_pos)
            {
                *tok_start = start;
                return -2;
            }
            start = i + 1;
        }
        if (i > 0 && !isspace(buffer[i]) && buffer[i] == ';')
            tok_counter++;
    }
    if (buffer[start] != '\0')
    {
        *tok_start = start;
        *tok_end = *position;
        return ++tok_counter;
    }
    return -1;
}

char **create_argv(char *token)
{
    glob_t glob_result;
    int ret = glob(token, GLOB_TILDE | GLOB_MARK, NULL, &glob_result);
    if (ret != 0)
    {
        globfree(&glob_result);
        return NULL;
    }
    int num_matches = glob_result.gl_pathc;
    char **list = malloc(TOK_BUF_SIZE * sizeof(char *));
    if (!list)
    {
        globfree(&glob_result);
        my_fprintf(stderr, "psh: allocation error\n");
        return NULL;
    }
    for (int i = 0; i < num_matches; i++)
        list[i] = strdup(glob_result.gl_pathv[i]);
    list[num_matches] = NULL;

    globfree(&glob_result);

    return list;
}

int path_exists(const char *path)
{
    struct stat path_stat;
    return (stat(path, &path_stat) == 0);
}

char **get_path_directories()
{
    char *path_env = getenv("PATH");
    if (!path_env)
        return NULL;
    int count = 1;
    for (char *p = path_env; *p; p++)
    {
        if (*p == ':')
            count++;
    }
    char **directories = malloc((count + 1) * sizeof(char *));
    if (!directories)
    {
        perror("malloc");
        return NULL;
    }
    char *path_copy = strdup(path_env);
    char *token = strtok(path_copy, ":");
    int i = 0;
    while (token)
    {
        if (path_exists(token))
            directories[i++] = strdup(token);
        token = strtok(NULL, ":");
    }
    directories[i] = NULL;
    free(path_copy);
    return directories;
}

void free_directories(char **dir)
{
    if (!dir)
        return;
    for (int i = 0; dir[i] != NULL; i++)
        free(dir[i]);
    free(dir);
}

char **create_cmd_argv(const char *pattern)
{
    char **directories = get_path_directories();
    if (!directories)
    {
        return NULL;
    }
    glob_t globbuf;
    memset(&globbuf, 0, sizeof(globbuf));

    for (int i = 0; directories[i] != NULL; i++)
    {
        char glob_pattern[1024];
        snprintf(glob_pattern, sizeof(glob_pattern), "%s/%s", directories[i], pattern);
        int ret = glob(glob_pattern, GLOB_APPEND, NULL, &globbuf);
        if (ret != 0 && ret != GLOB_NOMATCH)
        {
            perror("glob");
            globfree(&globbuf);
            return NULL;
        }
    }
    char **results = malloc((globbuf.gl_pathc + 1) * sizeof(char *));
    if (!results)
    {
        perror("malloc");
        globfree(&globbuf);
        return NULL;
    }
    for (size_t i = 0; i < globbuf.gl_pathc; i++)
        results[i] = strdup(basename(globbuf.gl_pathv[i]));
    results[globbuf.gl_pathc] = NULL;
    globfree(&globbuf);
    free_directories(directories);
    return results;
}

int is_executable(const char *file)
{
    struct stat st;
    if (stat(file, &st) == 0 && st.st_mode & S_IXUSR)
        return 1;
    return 0;
}

char *prepend_substring(char *token, const char *substring)
{
    size_t token_len = strlen(token);
    size_t substring_len = strlen(substring);
    size_t new_len = token_len + substring_len + 1;
    char *new_token = realloc(token, new_len);
    if (!new_token)
        exit(1);
    memmove(new_token + substring_len, new_token, token_len + 1);
    memcpy(new_token, substring, substring_len);
    return new_token;
}

char **create_exec_list(char *pattern)
{
    pattern += 2; // removing the ./ part
    char **list = create_argv(pattern);
    char **new_list = malloc(TOK_BUF_SIZE * sizeof(char *));
    if (!new_list)
        exit(1);
    int counter = 0;
    for (int i = 0; list[i] != NULL; i++)
    {
        if (is_executable(list[i]))
            new_list[counter++] = prepend_substring(list[i], "./");
        else
            free(list[i]);
    }
    free(list);
    new_list[counter] = NULL;
    return new_list;
}

void autocomplete(char *buffer, int *position, int *cursor_pos)
{
    if (token_to_complete && tab_count == 0)
    {
        free_tokens(possible_completions);
        free(token_to_complete);
    }
    char **tokens = tokenize(buffer);
    int *categories = categorize_tokens(tokens);

    if (tab_count == 0)
    {
        int tok_start = -1, tok_end = -1;
        int token_index = cursor_on_token_with_index(buffer, position, cursor_pos, &tok_start, &tok_end);
        int tok_category;
        if (token_index == -1)
        {
            int last_category;
            for (int i = 0; categories[i] != END; i++)
                last_category = categories[i];
            if (last_category == BG_OPER ||
                last_category == INVERSION ||
                last_category == PIPE ||
                last_category == OPER ||
                last_category == LINE_CONTINUATION)
                tok_category = CMD;
            else
                tok_category = ARG;
        }
        else if (token_index == -2)
            tok_category = ARG;
        else
            tok_category = categories[token_index];

        // my_printf("index %d\n", token_index);

        char *token = token_index == -1 || token_index == -2 ? strdup("") : strdup(tokens[token_index]);
        token_to_complete = append_star(token);
        word_start = tok_start;
        if (word_start == -1)
            word_start = *position;
        real_tok_category = tok_category;
    }

    // my_printf("\n");
    // my_printf("token %s\n", token_to_complete);
    // my_printf("word start %d\n", word_start);
    // my_printf("tok category %d\n", real_tok_category);
    // my_printf("tab_co %d\n", tab_count);

    /* Perform expansions ... */
    if (real_tok_category == 0)
    {
        /* cmd autocomplete */
        if (tab_count == 0)
        {
            if (startsWith(token_to_complete, "./"))
                possible_completions = create_exec_list(token_to_complete);
            else
                possible_completions = create_cmd_argv(token_to_complete);
        }
    }
    else if (real_tok_category == 1)
    {
        /* arg autocomplete */
        if (tab_count == 0)
            possible_completions = create_argv(token_to_complete);
    }
    else
    {
        free(categories);
        free_tokens(tokens);
        return;
    }

    if (possible_completions != NULL && possible_completions[0] != NULL)
    {
        int completion_count = 0;
        for (int i = 0; possible_completions[i] != NULL; i++)
            completion_count++;

        char *word_to_insert = possible_completions[tab_count % completion_count];
        int word_len = strlen(word_to_insert);

        int delete_len = 0;
        while (buffer[word_start + delete_len] != ' ' && buffer[word_start + delete_len] != '\0')
            delete_len++;

        int prefix_len = word_start;
        int suffix_len = *position - (word_start + delete_len);

        char prefix[prefix_len + 1];
        char suffix[suffix_len + 1];
        strncpy(prefix, buffer, prefix_len);
        prefix[prefix_len] = '\0';
        strncpy(suffix, &buffer[word_start + delete_len], suffix_len);
        suffix[suffix_len] = '\0';

        for (int i = *cursor_pos; i > 0; i--)
            printf("\b");

        for (int i = 0; i < prefix_len + delete_len + suffix_len; i++)
            printf(" ");

        for (int i = prefix_len + delete_len + suffix_len; i > 0; i--)
            printf("\b");

        snprintf(buffer, 1024, "%s%s%s", prefix, word_to_insert, suffix);

        *cursor_pos = word_start + word_len;
        *position = strlen(buffer);
        printf("%s", buffer);

        for (int i = *position; i > *cursor_pos; i--)
            printf("\b");
    }

    free_tokens(tokens);
    free(categories);
    return;
}