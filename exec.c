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
 * Applied after fork and before exec, so the file descriptors the new
 * program inherits are already the ones the user asked for.
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
    if (apply_redirs(c->redirs) == -1) {
        _exit(1);
    }
    if (c->argc == 0) {
        _exit(0); /* redirections only */
    }

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
 * taking the change with it. That means its redirections have to be undone
 * afterwards, or the shell would keep writing into the file forever.
 *
 * Also used for a command that is only redirections: a bare "> file"
 * creates or truncates it.
 */
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

static int run_pipeline(pipeline *pl) {
    expand_pipeline(pl);

    command *c = pl->cmds[0];
    if (c->argc == 0 || is_builtin(c->argv[0])) {
        return run_in_shell(c);
    }
    return run_external(c);
}

int run_cmdlist(cmdlist *cl) {
    int status = run_pipeline(cl->ao->pl);
    g_last_status = status;
    return status;
}
