#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include "shell.h"

/*
 * SIGCHLD handler — called automatically when any child process finishes.
 *
 * We loop with WNOHANG so we reap every child that has already exited
 * without blocking.  This prevents zombie processes from background jobs.
 */
static void handle_sigchld(int sig)
{
    (void)sig;
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        printf("\n[done] PID %d\naishell> ", pid);

    fflush(stdout);
}

/*
 * SIGINT handler — called when the user presses Ctrl+C.
 *
 * The shell itself ignores SIGINT (see setup_signals).
 * This handler only runs in the shell process; foreground children have
 * their SIGINT restored to SIG_DFL (see execute.c), so Ctrl+C kills them
 * but returns control to the shell prompt.
 */
static void handle_sigint(int sig)
{
    (void)sig;
    printf("\n");
    fflush(stdout);
}

/*
 * setup_signals() — register handlers once at shell startup.
 *
 * Shell process:
 *   SIGINT  → handle_sigint  (print newline, redisplay prompt)
 *   SIGCHLD → handle_sigchld (reap background children)
 *
 * Each forked child restores SIGINT to SIG_DFL before exec (execute.c).
 */
void setup_signals(void)
{
    signal(SIGINT,  handle_sigint);
    signal(SIGCHLD, handle_sigchld);
}
