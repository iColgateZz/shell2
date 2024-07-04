#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "custom_print.h"

extern History *last_history;
History *first_history = NULL;

#define HISTORY_MAX_SIZE 128
#define HISTORY_FILE ".psh_history"

int _count_history_size()
{
    short int counter = 0;
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

/* Free the linked list with history structs. */
void free_history()
{
    History *temp;
    History *head = first_history;
    while (head != NULL)
    {
        temp = head;
        head = head->next;
        free(temp->line);
        free(temp);
    }
}

/* Load the history from the file. */
void load_history()
{
    FILE *file = fopen(HISTORY_FILE, "r");
    int counter = 0;
    if (file)
    {
        char *line = NULL;
        size_t len = 0;
        while (getline(&line, &len, file) != -1 && counter < HISTORY_MAX_SIZE)
        {
            History *hist = malloc(sizeof(History));
            hist->next = NULL;
            hist->prev = last_history;
            hist->line = strdup(line);
            hist->line[strlen(hist->line) - 1] = 0;
            if (last_history)
                last_history->next = hist;
            else
                first_history = hist;
            last_history = hist;
            counter++;
        }
        free(line);
        fclose(file);
    }
}

/* Save the history to the file. */
void save_history()
{
    FILE *file = fopen(HISTORY_FILE, "w");
    if (file)
    {
        History *temp = first_history;
        while (temp)
        {
            fprintf(file, "%s\n", temp->line);
            temp = temp->next;
        }
        fclose(file);
    }

    free_history();
}

/* Add a command to the history. */
void add_to_history(const char *command)
{
    short int count = _count_history_size();
    // my_printf("H size is %d\n", count);
    History *new = malloc(sizeof(History));
    new->next = NULL;
    new->line = strdup(command);
    if (!first_history)
    {
        first_history = new;
        last_history = new;
        new->prev = NULL;
        return;
    }
    if (count >= HISTORY_MAX_SIZE)
    {
        History *temp = first_history->next;
        free(first_history->line);
        free(first_history);
        first_history = temp;
        first_history->prev = NULL;
    }
    new->prev = last_history;
    last_history->next = new;
    last_history = new;
}

/* List all command in the command history. */
void print_history()
{
    History *temp = first_history;
    short int counter = 1;
    while (temp)
    {
        my_printf("%d %s\n", counter, temp->line);
        counter++;
        temp = temp->next;
    }
}
