#ifndef HISTORY_H
#define HISTORY_H

typedef struct History
{
    struct History *next;
    struct History *prev;
    char *line;
} History;

void load_history();
void save_history();
void add_to_history(const char *command);
void print_history();

#endif