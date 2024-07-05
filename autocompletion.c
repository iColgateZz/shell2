#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "custom_print.h"
#include "helpers.h"
#include <ctype.h>
#include <glob.h>

#define TOK_BUF_SIZE 64

extern int tab_count;
char *token_to_complete = NULL;
int word_start = -1;
char **possible_completions = NULL;

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
            start = i + 1;
        }
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
    int ret = glob(token, GLOB_TILDE, NULL, &glob_result);
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

void autocomplete(char *buffer, int *position, int *cursor_pos)
{
    if (token_to_complete && tab_count == 0)
    {
        free_tokens(possible_completions);
        free(token_to_complete);
    }

    char **tokens = tokenize(buffer);
    int *categories = categorize_tokens(tokens);

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
    else
        tok_category = categories[token_index];

    if (tab_count == 0)
    {
        char *token = token_index == -1 ? strdup("") : strdup(tokens[token_index]);
        token_to_complete = append_star(token);
        word_start = tok_start;
        if (word_start == -1)
            word_start = *position;
    }

    /* Perform expansions ... */
    if (tok_category == 0)
    {
        /* cmd autocomplete */
    }
    else if (tok_category == 1)
    {
        /* arg autocomplete */
        if (tab_count == 0)
            possible_completions = create_argv(token_to_complete);
    }
    else
    {
        free_tokens(tokens);
        return;
    }

    // my_printf("Tok start is %d\n", word_start);
    if (possible_completions != NULL)
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

        for (int i = *cursor_pos; i < *position; i++)
            printf(" ");

        for (int i = 0; i < *position; i++)
            printf("\b \b");

        snprintf(buffer, 1024, "%s%s%s", prefix, word_to_insert, suffix);

        *cursor_pos = word_start + word_len;
        *position = strlen(buffer);
        printf("%s", buffer);

        for (int i = *position; i > *cursor_pos; i--)
            printf("\b");
    }

    free_tokens(tokens);
    return;
}