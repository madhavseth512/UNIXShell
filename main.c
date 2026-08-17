#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msh.h"

int main(void) {
    char *line = NULL;
    size_t cap = 0;

    /* Milestone 7 replaces this with sigaction and process groups. */
    signal(SIGINT, SIG_IGN);

    while (!g_exit_requested) {
        printf("msh> ");
        fflush(stdout);

        ssize_t nread = getline(&line, &cap, stdin);

        if (nread == -1) {
            if (ferror(stdin)) {
                perror("msh: getline");
                g_last_status = 1;
                break;
            }
            /* EOF. The prompt left the cursor mid-line. */
            putchar('\n');
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
    return g_exit_requested ? g_exit_code : g_last_status;
}
