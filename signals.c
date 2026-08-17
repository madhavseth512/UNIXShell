#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "msh.h"

volatile sig_atomic_t g_sigint = 0;

/*
 * The only thing a handler may safely do here is set a flag. Writing to
 * stdout from a handler would re-enter stdio, which is not async-signal-safe;
 * the REPL notices the flag when getline returns EINTR.
 */
static void on_sigint(int sig) {
    (void)sig;
    g_sigint = 1;
}

static void set_handler(int sig, void (*fn)(int), int flags) {
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;

    if (sigaction(sig, &sa, NULL) == -1) {
        perror("msh: sigaction");
    }
}

/*
 * sigaction, not signal: signal()'s semantics diverged between BSD and
 * System V over whether the handler resets on delivery and whether slow
 * syscalls restart. sigaction states both explicitly.
 *
 * SIGINT is caught rather than ignored, and deliberately without SA_RESTART,
 * so a blocked getline returns -1 with errno EINTR and the REPL can redraw
 * the prompt. SIGTTOU must be ignored or tcsetpgrp() below would stop the
 * shell: a background process calling it receives SIGTTOU by definition.
 */
void signals_init(void) {
    set_handler(SIGINT, on_sigint, 0);
    set_handler(SIGQUIT, SIG_IGN, 0);
    set_handler(SIGTSTP, SIG_IGN, 0);
    set_handler(SIGTTIN, SIG_IGN, 0);
    set_handler(SIGTTOU, SIG_IGN, 0);
}

/*
 * Dispositions survive exec: SIG_IGN stays ignored in the new program, so
 * without this the child would inherit the shell's immunity to Ctrl-C and
 * "sleep 30" would be unkillable.
 */
void signals_child_default(void) {
    set_handler(SIGINT, SIG_DFL, 0);
    set_handler(SIGQUIT, SIG_DFL, 0);
    set_handler(SIGTSTP, SIG_DFL, 0);
    set_handler(SIGTTIN, SIG_DFL, 0);
    set_handler(SIGTTOU, SIG_DFL, 0);
}
