#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "msh.h"

int g_last_status = 0;

/* ------------------------------------------------------------------ *
 * $? expansion
 *
 * Done at execution time, not parse time. "false; echo $?" is parsed as a
 * whole before anything runs, so expanding during the parse would capture
 * the status from before false ever executed.
 * ------------------------------------------------------------------ */

static int expand_word(char **slot) {
    const char *src = *slot;
    if (strstr(src, "$?") == NULL) {
        return 0;
    }

    char num[16];
    int nlen = snprintf(num, sizeof num, "%d", g_last_status);
    if (nlen < 0) {
        return -1;
    }

    size_t hits = 0;
    for (const char *p = src; (p = strstr(p, "$?")) != NULL; p += 2) {
        hits++;
    }

    size_t size = strlen(src) + hits * ((size_t)nlen - 2) + 1;
    char *out = malloc(size);
    if (out == NULL) {
        return -1;
    }

    char *dst = out;
    for (const char *p = src; *p != '\0';) {
        if (p[0] == '$' && p[1] == '?') {
            memcpy(dst, num, (size_t)nlen);
            dst += nlen;
            p += 2;
        } else {
            *dst++ = *p++;
        }
    }
    *dst = '\0';

    free(*slot);
    *slot = out;
    return 0;
}

static void expand_pipeline(pipeline *pl) {
    for (int i = 0; i < pl->ncmds; i++) {
        command *c = pl->cmds[i];
        for (int j = 0; j < c->argc; j++) {
            if (expand_word(&c->argv[j]) == -1) {
                fprintf(stderr, "msh: out of memory\n");
            }
        }
    }
}

/* ------------------------------------------------------------------ *
 * Running one command
 * ------------------------------------------------------------------ */

static int status_from_wait(int wstatus) {
    if (WIFEXITED(wstatus)) {
        return WEXITSTATUS(wstatus);
    }
    if (WIFSIGNALED(wstatus)) {
        return 128 + WTERMSIG(wstatus);
    }
    return 1;
}

static void child_exec(command *c) {
    execvp(c->argv[0], c->argv);

    /* Only reachable if exec failed: the image was never replaced. */
    fprintf(stderr, "msh: %s: %s\n", c->argv[0], strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

static int run_external(command *c) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("msh: fork");
        return 1;
    }
    if (pid == 0) {
        child_exec(c);
    }

    int wstatus;
    if (waitpid(pid, &wstatus, 0) == -1) {
        perror("msh: waitpid");
        return 1;
    }
    return status_from_wait(wstatus);
}

/*
 * A builtin runs in the shell process itself. chdir() and exit() act on the
 * caller, so a forked child would change its own directory and then die,
 * taking the change with it.
 */
static int run_pipeline(pipeline *pl) {
    expand_pipeline(pl);

    command *c = pl->cmds[0];
    if (is_builtin(c->argv[0])) {
        int status = run_builtin(c);
        fflush(stdout);
        return status;
    }
    return run_external(c);
}

int run_cmdlist(cmdlist *cl) {
    int status = run_pipeline(cl->ao->pl);
    g_last_status = status;
    return status;
}
