#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern History *last_history;
History *first_history;

#define HISTORY_MAX_SIZE 128
#define HISTORY_FILE ".psh_history"

int _count_history_size()
{
    char counter = 0;
    History *temp = last_history;
    if (!temp)
        return 0;
    while (temp)
    {
        counter++;
        temp = temp->prev;
    }
    return counter;
}

void load_history()
{
    FILE *file = fopen(HISTORY_FILE, "r");
    if (file)
    {
        char *line = NULL;
        size_t len = 0;
        while (getline(&line, &len, file) != -1)
        {
            History *hist = malloc(1);
            hist->next = NULL;
            hist->prev = NULL;
            hist->line = strdup(line);
            if (last_history)
            {
                hist->prev = last_history;
                last_history->next = hist;
            }
            else
                first_history = hist;
            last_history = hist;
        }
        free(line);
        fclose(file);
    }
}

void save_history()
{
    FILE *file = fopen(HISTORY_FILE, "w");
    if (file)
    {
        History *temp = first_history;
        while (temp)
        {
            fprintf(file, "%s", temp->line);
            temp = temp->next;
        }
        fclose(file);
    }
}
