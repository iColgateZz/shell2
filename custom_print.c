#include <stdio.h>
#include <stdarg.h>


/* Overriden default printing functions. 
   The are suitable for both the raw and the normal terminal modes. */

extern int shell_is_interactive;

void my_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    if (shell_is_interactive)
    {
        printf("\r");
        fflush(stdout);
    }
}

void my_fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);
    if (shell_is_interactive)
    {
        fprintf(stream, "\r");
        fflush(stream);
    }
}

void my_perror(const char *s)
{
    perror(s);
    if (shell_is_interactive)
    {
        fprintf(stderr, "\r");
        fflush(stderr);
    }
}
