#include <stdio.h>
#include <stdlib.h>
#include "helpers.h"

int main(void)
{
    char arr[] = "hello &";
    printf("Output is %d\n", !endsWith(arr, '&'));
    return 0;
}