#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "msh.h"

static void prompt(void) {
    if (g_interactive) {
        printf("msh> ");
        fflush(stdout);
    }
}

/*
 * Put the shell in its own process group and give that group the terminal.
 * Without this, every child would share the shell's group and a Ctrl-C would
 * hit both. Skipped when stdin is not a terminal, where there is no
 * foreground group to hand around.
 */
static void take_terminal(void) {
    g_shell_pgid = getpid();

    /* A session leader is already its own process-group leader, and setpgid
       refuses to move one (EPERM). Only relocate the shell if it is not
       there yet -- otherwise a login shell would report a spurious error. */
    if (getpgrp() != g_shell_pgid &&
        setpgid(g_shell_pgid, g_shell_pgid) == -1) {
        perror("msh: setpgid");
        g_shell_pgid = getpgrp(); /* carry on with the group we are in */
    }
    if (tcsetpgrp(STDIN_FILENO, g_shell_pgid) == -1) {
        perror("msh: tcsetpgrp");
        g_interactive = 0;
    }
}

int main(void) {
    char *line = NULL;
    size_t cap = 0;

    g_interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);
    signals_init();
    if (g_interactive) {
        take_terminal();
    }

    while (!g_exit_requested) {
        g_sigint = 0;
        prompt();

        errno = 0;
        ssize_t nread = getline(&line, &cap, stdin);

        if (nread == -1) {
            /* A caught SIGINT aborts the read. Without SA_RESTART getline
               returns -1/EINTR, and treating that as EOF would make Ctrl-C
               close the shell -- the opposite of the intent. */
            if (g_sigint || errno == EINTR) {
                clearerr(stdin);
                g_sigint = 0;
                if (g_interactive) {
                    putchar('\n');
                }
                g_last_status = 130; /* 128 + SIGINT */
                continue;
            }
            if (ferror(stdin)) {
                perror("msh: getline");
                g_last_status = 1;
                break;
            }
            /* EOF. The prompt left the cursor mid-line. */
            if (g_interactive) {
                putchar('\n');
            }
            break;
        }

        /* getline keeps the newline; an unstripped one becomes part of a
           token. Use nread, not strlen, and allow for a final line that has
           none because the input ended without one. */
        if (nread > 0 && line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }

        token_list tokens;
        if (lex(line, &tokens) == -1) {
            g_last_status = 2;
            continue;
        }

        int syntax_err = 0;
        cmdlist *cl = parse(&tokens, &syntax_err);
        token_list_free(&tokens);

        if (cl == NULL) {
            if (syntax_err) {
                g_last_status = 2; /* sh reports 2 for a syntax error */
            }
            continue; /* otherwise the line was blank */
        }

        g_last_status = run_cmdlist(cl);
        cmdlist_free(cl);
    }

    free(line);

    if (g_interactive) {
        tcsetpgrp(STDIN_FILENO, g_shell_pgid);
    }
    return g_exit_requested ? g_exit_code : g_last_status;
}
