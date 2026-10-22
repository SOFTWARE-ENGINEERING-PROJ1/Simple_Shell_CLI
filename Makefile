CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c11 -g
TARGET  = shell
SRCS    = main.c lexer.c expand.c builtins.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	SHELL_BIN=./$(TARGET) sh tests/test_builtins.sh
	SHELL_BIN=./$(TARGET) sh tests/test_external.sh
	SHELL_BIN=./$(TARGET) sh tests/test_pipelines.sh

clean:
	rm -f $(OBJS) $(TARGET)