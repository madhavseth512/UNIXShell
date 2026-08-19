#include <errno.h>
#include <fcntl.h>
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
        for (redir *r = c->redirs; r != NULL; r = r->next) {
            if (expand_word(&r->target) == -1) {
                fprintf(stderr, "msh: out of memory\n");
            }
        }
    }
}

/* ------------------------------------------------------------------ *
 * Redirection
 * ------------------------------------------------------------------ */

/*
 * Applied after fork and after the pipe wiring, so an explicit redirection
 * overrides the pipe: in "a | b > f", b's stdout is f, not the pipe.
 */
static int apply_redirs(redir *r) {
    for (; r != NULL; r = r->next) {
        int fd;
        int target;

        switch (r->kind) {
        case REDIR_IN:
            fd = open(r->target, O_RDONLY);
            target = STDIN_FILENO;
            break;
        case REDIR_OUT:
            fd = open(r->target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            target = STDOUT_FILENO;
            break;
        case REDIR_APPEND:
            fd = open(r->target, O_WRONLY | O_CREAT | O_APPEND, 0644);
            target = STDOUT_FILENO;
            break;
        default:
            return -1;
        }

        if (fd == -1) {
            fprintf(stderr, "msh: %s: %s\n", r->target, strerror(errno));
            return -1;
        }
        if (dup2(fd, target) == -1) {
            perror("msh: dup2");
            close(fd);
            return -1;
        }
        close(fd); /* dup2 already installed it at the well-known number */
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * Running one command in the shell process
 *
 * Used for a lone builtin, and for a command that is only redirections
 * (a bare "> file" creates or truncates it). Redirections have to be undone
 * afterwards, or the shell would keep writing into the file forever.
 * ------------------------------------------------------------------ */

static int run_in_shell(command *c) {
    int saved_in = -1;
    int saved_out = -1;
    int status = 0;

    if (c->redirs != NULL) {
        saved_in = dup(STDIN_FILENO);
        saved_out = dup(STDOUT_FILENO);
        if (saved_in == -1 || saved_out == -1) {
            perror("msh: dup");
            if (saved_in != -1) {
                close(saved_in);
            }
            if (saved_out != -1) {
                close(saved_out);
            }
            return 1;
        }
        if (apply_redirs(c->redirs) == -1) {
            status = 1;
        }
    }

    if (status == 0 && c->argc > 0) {
        status = run_builtin(c);
    }

    /* Flush before restoring: buffered output still belongs to the redirect. */
    fflush(stdout);

    if (saved_in != -1) {
        dup2(saved_in, STDIN_FILENO);
        close(saved_in);
    }
    if (saved_out != -1) {
        dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
    }
    return status;
}

/* ------------------------------------------------------------------ *
 * Pipelines
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
    if (apply_redirs(c->redirs) == -1) {
        _exit(1);
    }
    if (c->argc == 0) {
        _exit(0); /* redirections only */
    }
    /* A builtin inside a pipeline runs in the child, where cd and exit
       affect only that short-lived process. That is what sh does too. */
    if (is_builtin(c->argv[0])) {
        int rc = run_builtin(c);
        fflush(stdout);
        _exit(rc);
    }

    execvp(c->argv[0], c->argv);

    /* Only reachable if exec failed: the image was never replaced. */
    fprintf(stderr, "msh: %s: %s\n", c->argv[0], strerror(errno));
    _exit(errno == ENOENT ? 127 : 126);
}

static int run_pipeline(pipeline *pl) {
    expand_pipeline(pl);

    if (pl->ncmds == 1) {
        command *c = pl->cmds[0];
        if (c->argc == 0 || is_builtin(c->argv[0])) {
            return run_in_shell(c);
        }
    }

    pid_t *pids = malloc((size_t)pl->ncmds * sizeof *pids);
    if (pids == NULL) {
        fprintf(stderr, "msh: out of memory\n");
        return 1;
    }

    int prev_read = -1; /* read end of the pipe feeding this stage */
    int started = 0;
    int failed = 0;

    for (int i = 0; i < pl->ncmds; i++) {
        int fd[2] = {-1, -1};

        if (i < pl->ncmds - 1 && pipe(fd) == -1) {
            perror("msh: pipe");
            failed = 1;
            break;
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("msh: fork");
            if (fd[0] != -1) {
                close(fd[0]);
                close(fd[1]);
            }
            failed = 1;
            break;
        }

        if (pid == 0) {
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            if (fd[1] != -1) {
                close(fd[0]); /* this child never reads its own pipe */
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
            }
            child_exec(pl->cmds[i]);
        }

        if (prev_read != -1) {
            close(prev_read);
        }
        if (fd[1] != -1) {
            /* The shell must close every pipe end it does not need, or the
               reader never sees EOF and the pipeline hangs forever. */
            close(fd[1]);
            prev_read = fd[0];
        }
        pids[started++] = pid;
    }

    if (prev_read != -1) {
        close(prev_read);
    }

    if (started == 0) {
        free(pids);
        return 1;
    }

    int status = 1;
    for (int i = 0; i < started; i++) {
        int wstatus;
        if (waitpid(pids[i], &wstatus, 0) == -1) {
            perror("msh: waitpid");
            continue;
        }
        if (i == started - 1) {
            status = status_from_wait(wstatus);
        }
    }

    free(pids);
    return failed && started == 0 ? 1 : status;
}

int run_cmdlist(cmdlist *cl) {
    int status = run_pipeline(cl->ao->pl);
    g_last_status = status;
    return status;
}
