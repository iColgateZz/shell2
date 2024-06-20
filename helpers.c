#include <stdlib.h>
#include <string.h>

char *operators[] = {
    ";",
    "&&",
    "||",
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

int endsWith(const char *str, char c)
{
    size_t len = strlen(str);
    return str[len - 1] == c;
}