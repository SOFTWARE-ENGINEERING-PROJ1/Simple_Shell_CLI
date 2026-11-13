CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c11 -g
LDLIBS  ?= -lreadline
TARGET  = shell
SRCS    = main.c lexer.c expand.c builtins.c signals.c interactive.c jobs.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean test memcheck s9-validation

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c shell.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	SHELL_BIN=./$(TARGET) bash tests/test_builtins.sh
	SHELL_BIN=./$(TARGET) bash tests/test_external.sh
	SHELL_BIN=./$(TARGET) bash tests/test_pipelines.sh
	SHELL_BIN=./$(TARGET) bash tests/test_s5.sh
	SHELL_BIN=./$(TARGET) bash tests/test_interactive.sh
	SHELL_BIN=./$(TARGET) bash tests/test_s8_background.sh
	SHELL_BIN=./$(TARGET) bash tests/test_s8_chaining.sh
	python3 tests/test_interactive.py ./$(TARGET)
	python3 tests/test_tty.py ./$(TARGET)
	python3 tests/test_s8_jobs.py ./$(TARGET)

memcheck: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=definite,indirect \
		--error-exitcode=1 ./$(TARGET) < tests/memcheck_commands.txt > /dev/null

s9-validation:
	bash scripts/run_s9_validation.sh

clean:
	rm -f $(OBJS) $(TARGET)
