#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

char *operators[] = {
    ";",
    "&&",
    "||",
    NULL
};

char *redirection[] = {
    ">",
    ">>",
    "<",
    "2>",
    NULL
};

int isOperator(char *str)
{
    for (int i = 0; operators[i] != NULL; i++)
    {
        if (strcmp(operators[i], str) == 0)
            return 1;
    }
    return 0;
}

int isRedirection(char *str)
{
    for (int i = 0; redirection[i] != NULL; i++)
    {
        if (strcmp(redirection[i], str) == 0)
            return 1;
    }
    return 0;
}

int endsWith(const char *str, char c)
{
    size_t len = strlen(str);
    return str[len - 1] == c;
}

char *trim(char *str)
{
    char *end;

    while (isspace((unsigned char)*str))
        str++;

    if (*str == 0)
    {
        return str;
    }

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;

    *(end + 1) = 0;

    return str;
}

char *concat_line(char **tokens, int start, int end) 
{
    char *str = malloc(256 * sizeof(char));
    if (!str)
    {
        perror("allocation error");
        exit(1);
    }
    int pos = 0;
    for (int i = start; i < end; i++) {
        for (int j = 0; tokens[i][j] != '\0'; j++) {
            str[pos++] = tokens[i][j];
        }
        str[pos++] = ' ';
    }
    return str;
}

int containsChar(char *word, char c)
{
    for (int i = 0; word[i] != '\0'; i++)
    {
        if (word[i] == c)
            return 1;
    }
    return 0;
}

int count_elem_in_list(char **list)
{
    int counter = 0;
    while (list[counter] != NULL)
        counter++;
    
    return counter;
}