#ifndef HISTORY_H
#define HISTORY_H

typedef struct History
{
    struct History *next;
    struct History *prev;
    char *line;
} History;

#endif