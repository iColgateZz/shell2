#ifndef BUILT_INS_H
#define BUILT_INS_H

typedef int (*builtin_func)(char **);

extern char *builtin_str[];
extern builtin_func func_arr[];

int psh_num_builtins();
int psh_cd(char **args);
int psh_help(char **args);
int psh_exit(char **args);

#endif