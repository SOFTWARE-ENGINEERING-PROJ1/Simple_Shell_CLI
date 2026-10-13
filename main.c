#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROMPT "simpleshell> "

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    while (1) {
        printf("%s", PROMPT);
        fflush(stdout);

        nread = getline(&line, &len, stdin);

        // 1. Handle EOF (Ctrl+D on Linux / Ctrl+Z+Enter on Windows)
        if (nread == -1) {
            printf("\nExiting shell...\n");
            break;
        }

        // 2. Remove trailing newline
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
            nread--;
        }

        // 3. Ignore blank lines
        if (nread == 0) {
            continue;
        }

        // 4. Clean exit if user types 'exit' or 'quit'
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            printf("Exiting shell...\n");
            break;
        }

        printf("Parsed command: [%s]\n", line);
    }

    free(line);
    return 0;
}