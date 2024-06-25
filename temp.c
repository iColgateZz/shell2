#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "helpers.h"

int main(void)
{
    char *str = malloc(16 * sizeof(char));
    str = strdup("string");

    printf("Len of str is %d\n", strlen(str));
    str[0] = '\0';

    printf("Len of str is %d\n", strlen(str));

    endsWith(NULL, '"');
    printf("got here?");
    return 0;
}