#include <stdio.h>
#include <stdarg.h>

void my_printf(const char *format, ...);
void my_fprintf(FILE *stream, const char *format, ...);
void my_perror(const char *s);