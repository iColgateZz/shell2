#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "env.h"
#include "custom_print.h"

#define TOK_BUF_SIZE 64

char *cursor_on_token(char *buffer, int *position, int *cursor_pos)
{
    char *return_value;
    char arr[TOK_BUF_SIZE];
    int start = 0, end = 0;

    int c_p_copy = *cursor_pos;
    if (c_p_copy == position)
        c_p_copy--;
    if ()

    for (int i = *cursor_pos; buffer[i] != ' '; i--)
    {

    }
    
}

void autocomplete(char *buffer, int *position, int *cursor_pos)
{
    // char **tokens = tokenize(buffer);
    // int *categories = categorize_tokens(tokens);

    char *token_to_complete = cursor_on_token(buffer, position, cursor_pos);
    if (!token_to_complete)
        return;
    
    int last_token_category;
}