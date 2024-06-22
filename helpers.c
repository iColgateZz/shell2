#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

char *operators[] = {
    ";",
    "&&",
    "||",
    NULL};

int isOperator(char *str)
{
    for (int i = 0; operators[i] != NULL; i++)
    {
        if (strcmp(operators[i], str) == 0)
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