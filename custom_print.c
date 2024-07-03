#include <stdio.h>
#include <stdarg.h>

void my_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\r");
    fflush(stdout);
}

void my_fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stream, format, args);
    va_end(args);
    fprintf(stream, "\r");
    fflush(stream);
}

void my_perror(const char *s)
{
    perror(s);
    fprintf(stderr, "\r");
    fflush(stderr);
}
