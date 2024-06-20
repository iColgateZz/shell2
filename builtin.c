#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "builtin.h"

// Function prototypes for built-in commands
int psh_cd(char **args);
int psh_help(char **args);
int psh_exit(char **args);

// Array of built-in command strings
char *builtin_str[] = {
    "cd",
    "help",
    "exit"
};

// Array of built-in command function pointers
builtin_func func_arr[] = {
    &psh_cd,
    &psh_help,
    &psh_exit
};

int psh_num_builtins() {
    return sizeof(builtin_str) / sizeof(char *);
}

int psh_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "psh: expected argument to \"cd\"\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("psh");
        }
    }
    return 1;
}

int psh_help(char **args) {
    int i;
    printf("PSH\n");
    printf("Type program names and arguments, and hit enter.\n");
    printf("The following are built in:\n");
    for (i = 0; i < psh_num_builtins(); i++) {
        printf("  %s\n", builtin_str[i]);
    }
    printf("Use the \"man\" command for information on other programs.\n");
    return 1;
}

int psh_exit(char **args) {
    return 0;
}