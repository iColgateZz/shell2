#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <string.h>

#define BUF_SIZE 1024

struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void handle_sigwinch(int sig) {
    // Handle window size change if needed
}

void init_line_editing() {
    enable_raw_mode();
    signal(SIGWINCH, handle_sigwinch);
}

void read_line(char *buffer) {
    int position = 0;
    int c;

    while (1) {
        c = getchar();

        if (c == '\n' || c == '\r') {
            buffer[position] = '\0';
            printf("\n");
            return;
        } else if (c == 127) { // Handle backspace
            if (position > 0) {
                position--;
                printf("\b \b");
            }
        } else if (c == 1) { // Handle Ctrl-A (move to beginning)
            while (position > 0) {
                printf("\b");
                position--;
            }
        } else if (c == 5) { // Handle Ctrl-E (move to end)
            while (buffer[position] != '\0') {
                printf("%c", buffer[position]);
                position++;
            }
        } else if (c == 23) { // Handle Ctrl-W (delete word)
            while (position > 0 && buffer[position - 1] == ' ') {
                position--;
                printf("\b \b");
            }
            while (position > 0 && buffer[position - 1] != ' ') {
                position--;
                printf("\b \b");
            }
        } else if (c >= 32 && c <= 126) { // Printable characters
            buffer[position++] = c;
            printf("%c", c);
        }
    }
}

int main() {
    char buffer[BUF_SIZE];

    init_line_editing();

    while (1) {
        printf("prompt> ");
        read_line(buffer);
        printf("You entered: %s\n", buffer);
    }

    return 0;
}