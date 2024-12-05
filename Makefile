# Compiler to use
CC = gcc

# compiler flags
CFLAGS = -Wall -Wextra -pedantic -std=c99

# Output name
TARGET = kilo

# source files 
SRCS = $(wildcard *.c)

# Object files replace .c with .o
OBJS = $(SRCS:.c=.o)

# Header files
DEPS = $(wildcard *.h)

#Default target
all: $(TARGET)

# Link object files to create executable
$(TARGET): $(OBJS)
		$(CC) $(OBJS) -o $(TARGET)

# Compile source files to object files
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJS) $(TARGET)

delete-logs:
	rm -rf *.log

.PHONY: all clean
