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
	./tests/test_builtins.sh

clean:
	rm -f $(OBJS) $(TARGET)
