#include <errno.h>
#include <signal.h>
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
 * wait.
 */
static int launch(char *argv[]) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("msh: fork");
        return -1;
    }

    if (pid == 0) {
        /* Child process: restore default Ctrl+C behavior so it can be killed */
        signal(SIGINT, SIG_DFL);
        
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
    
    int last_exit_status = 0;

    /* Ignore Ctrl+C in the main parent shell */
    signal(SIGINT, SIG_IGN);

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

        /* Substitute $? with the actual last exit status */
        char *allocated_args[MAX_ARGS] = {NULL};
        for (int i = 0; i < argc; i++) {
            if (strcmp(argv[i], "$?") == 0) {
                char *status_str = malloc(16);
                if (status_str) {
                    snprintf(status_str, 16, "%d", last_exit_status);
                    argv[i] = status_str; 
                    allocated_args[i] = status_str; /* Track to free later */
                }
            }
        }

        /* Handle Built-in Commands */
        int is_builtin = 0;
        if (strcmp(argv[0], "exit") == 0) {
            break; /* Exit the shell loop */
        } else if (strcmp(argv[0], "cd") == 0) {
            is_builtin = 1;
            if (argv[1] == NULL) {
                fprintf(stderr, "msh: cd: missing argument\n");
            } else {
                if (chdir(argv[1]) != 0) {
                    perror("msh: cd");
                }
            }
        }

        /* Execute external commands if it wasn't a built-in */
        if (!is_builtin) {
            int raw_status = launch(argv);
            if (raw_status != -1) {
                /* Extract the real exit status from the waitpid bitfield */
                if (WIFEXITED(raw_status)) {
                    last_exit_status = WEXITSTATUS(raw_status);
                } else if (WIFSIGNALED(raw_status)) {
                    last_exit_status = 128 + WTERMSIG(raw_status);
                }
            }
        }

        /* Free dynamically allocated strings used for $? substitution */
        for (int i = 0; i < argc; i++) {
            if (allocated_args[i] != NULL) {
                free(allocated_args[i]);
            }
        }
    }

    free(line);
    return 0;
}