CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c11 -g
TARGET  = shell
SRCS    = main.c lexer.c expand.c builtins.c signals.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean test memcheck

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	SHELL_BIN=./$(TARGET) sh tests/test_builtins.sh
	SHELL_BIN=./$(TARGET) sh tests/test_external.sh
	SHELL_BIN=./$(TARGET) sh tests/test_pipelines.sh
	SHELL_BIN=./$(TARGET) sh tests/test_s5.sh
	@if command -v python3 >/dev/null 2>&1; then \
		python3 tests/test_tty.py ./$(TARGET); \
	else \
		echo "SKIP  Ctrl-C/Ctrl-Z terminal tests (python3 not found)"; \
	fi

# Valgrind over 50+ commands including error paths (S5 §8 / NFR-02)
memcheck: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect \
		--error-exitcode=1 ./$(TARGET) < tests/memcheck_commands.txt > /dev/null

clean:
	rm -f $(OBJS) $(TARGET)