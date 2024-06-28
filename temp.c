#include <stdlib.h>
#include <stdio.h>

int main(void) 
{
    printf("String is %s\n", getenv("USER"));
    setenv("LOL", "HAHA", 1);
    printf("String is %s\n", getenv("LOL"));
    return 0;
}