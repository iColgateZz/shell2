#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char **tokens = malloc(64 * sizeof(char *));

    tokens[0] = "ASDA";
    tokens[1] = "ASDA";
    tokens[2] = "ASDA";
    tokens[3] = "ASDA";
    tokens[5] = NULL;

    printf("Size of this array is %d\n", (sizeof(tokens) / sizeof(char *)));
    return 0;
}