#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msh.h"

int g_exit_requested = 0;
int g_exit_code = 0;

/*
 * getcwd with a buffer that grows until the path fits. POSIX leaves
 * getcwd(NULL, 0) unspecified, so we do not rely on the glibc extension.
 */
static char *current_dir(void) {
    size_t size = 256;
    char *buf = NULL;

    for (;;) {
        char *tmp = realloc(buf, size);
        if (tmp == NULL) {
            free(buf);
            return NULL;
        }
        buf = tmp;
        if (getcwd(buf, size) != NULL) {
            return buf;
        }
        if (errno != ERANGE) {
            free(buf);
            return NULL;
        }
        size *= 2;
    }
}

/*
 * cd must run in the shell itself. chdir() changes the calling process's
 * working directory; a forked child would change its own and then exit,
 * taking the change with it.
 */
static int builtin_cd(command *c) {
    const char *target;

    if (c->argc > 2) {
        fprintf(stderr, "msh: cd: too many arguments\n");
        return 1;
    }

    if (c->argc < 2) {
        target = getenv("HOME");
        if (target == NULL || *target == '\0') {
            fprintf(stderr, "msh: cd: HOME not set\n");
            return 1;
        }
    } else {
        target = c->argv[1];
    }

    char *old = current_dir(); /* may be NULL; OLDPWD is best-effort */

    if (chdir(target) == -1) {
        fprintf(stderr, "msh: cd: %s: %s\n", target, strerror(errno));
        free(old);
        return 1;
    }

    if (old != NULL) {
        setenv("OLDPWD", old, 1);
        free(old);
    }
    char *new = current_dir();
    if (new != NULL) {
        setenv("PWD", new, 1);
        free(new);
    }
    return 0;
}

static int builtin_pwd(command *c) {
    (void)c;
    char *cwd = current_dir();
    if (cwd == NULL) {
        fprintf(stderr, "msh: pwd: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", cwd);
    free(cwd);
    return 0;
}

/*
 * exit cannot fork either: the process that must terminate is this one.
 * With no argument it reports the status of the last command, matching sh.
 */
static int builtin_exit(command *c) {
    int code = g_last_status;

    if (c->argc > 2) {
        fprintf(stderr, "msh: exit: too many arguments\n");
        return 1; /* sh does not exit in this case */
    }

    if (c->argc == 2) {
        char *end;
        errno = 0;
        long value = strtol(c->argv[1], &end, 10);
        if (end == c->argv[1] || *end != '\0' || errno == ERANGE) {
            fprintf(stderr, "msh: exit: %s: numeric argument required\n",
                    c->argv[1]);
            code = 2;
        } else {
            /* Only the low 8 bits survive wait(); mirror that here. */
            code = (int)(value & 0xff);
        }
    }

    g_exit_requested = 1;
    g_exit_code = code;
    return code;
}

int is_builtin(const char *name) {
    return strcmp(name, "cd") == 0 || strcmp(name, "pwd") == 0 ||
           strcmp(name, "exit") == 0;
}

int run_builtin(command *c) {
    if (strcmp(c->argv[0], "cd") == 0) {
        return builtin_cd(c);
    }
    if (strcmp(c->argv[0], "pwd") == 0) {
        return builtin_pwd(c);
    }
    if (strcmp(c->argv[0], "exit") == 0) {
        return builtin_exit(c);
    }
    return 1; /* unreachable while is_builtin and this stay in sync */
}
