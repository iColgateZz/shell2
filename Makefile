# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -g

# Executable name
TARGET = psh

# Source files
SRCS = main.c builtin.c helpers.c env.c custom_print.c history.c autocompletion.c

# Object files
OBJS = $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Rule to link object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean