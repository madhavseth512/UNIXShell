#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* argv[] holds at most MAX_ARGS - 1 tokens plus the NULL terminator. */
#define MAX_ARGS 64

/*
 * Split line in place on spaces and tabs.
 *
 * Returns the token count, or -1 if the line has too many tokens. argv is
 * always NULL-terminated on success. The tokens point into line, so line must
 * outlive argv.
 */
static int tokenize(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *saveptr = NULL;

    for (char *tok = strtok_r(line, " \t", &saveptr);
         tok != NULL;
         tok = strtok_r(NULL, " \t", &saveptr)) {
        if (argc == max_args - 1) {
            fprintf(stderr, "msh: too many arguments (max %d)\n", max_args - 1);
            return -1;
        }
        argv[argc++] = tok;
    }

    argv[argc] = NULL;
    return argc;
}

/*
 * Run argv in a child process and wait for it.
 *
 * Returns the child's wait status, or -1 if the shell itself failed to fork or
 * wait. Nothing here consumes the status yet; milestone 6 turns it into $?.
 */
static int launch(char *argv[]) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("msh: fork");
        return -1;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        /* Only reachable if exec failed: the image was never replaced. */
        fprintf(stderr, "msh: %s: %s\n", argv[0], strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("msh: waitpid");
        return -1;
    }
    return status;
}

int main(void) {
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    char *argv[MAX_ARGS];

    while (1) {
        printf("msh> ");
        fflush(stdout);

        nread = getline(&line, &len, stdin);
        if (nread == -1) {
            if (ferror(stdin)) {
                perror("msh: getline");
                free(line);
                return 1;
            }
            /* EOF: the prompt has no newline, so supply one before leaving. */
            putchar('\n');
            break;
        }

        /* getline keeps the newline; an unstripped one becomes part of a token.
           Use nread, not strlen, and allow for a final line that has none. */
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc <= 0) {
            continue; /* blank line (0) or too many arguments (-1) */
        }

        launch(argv);
    }

    free(line);
    return 0;
}
