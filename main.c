#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    while (1) {
        // 1. Print the prompt
        printf("msh> ");
        fflush(stdout);

        // 2. Read a line of arbitrary length
        nread = getline(&line, &len, stdin);

        // 5. Exit cleanly on Ctrl-D (EOF)
        if (nread == -1) {
            break;
        }

        // 3. Echo it back
        printf("%s", line);
    }

    // Free the dynamically allocated buffer before exiting
    free(line);
    return 0;
}
