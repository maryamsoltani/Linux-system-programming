#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "shell.h"

/* ── single command ───────────────────────────────────────────────────────── */

int launch(Command *cmd)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* child: restore SIGINT so Ctrl+C kills this process, not ignored */
        signal(SIGINT, SIG_DFL);

        if (execvp(cmd->argv[0], cmd->argv) == -1)
            perror(cmd->argv[0]);
        exit(EXIT_FAILURE);
    }

    if (cmd->background) {
        printf("[background] PID %d\n", pid);
        return 0;   /* SIGCHLD handler will reap it when it finishes */
    }

    /* foreground: wait for child */
    int status;
    do { waitpid(pid, &status, WUNTRACED); }
    while (!WIFEXITED(status) && !WIFSIGNALED(status));
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* ── pipeline ─────────────────────────────────────────────────────────────── */

int run_pipeline(Pipeline *pl)
{
    int   infd = STDIN_FILENO;
    pid_t pids[MAX_CMDS];
    int   n = pl->num_cmds;

    for (int i = 0; i < n; i++) {
        int fd[2];

        if (i < n - 1 && pipe(fd) < 0) {
            perror("pipe");
            return 1;
        }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }

        if (pid == 0) {
            /* child: restore SIGINT to default */
            signal(SIGINT, SIG_DFL);

            if (infd != STDIN_FILENO) {
                dup2(infd, STDIN_FILENO);
                close(infd);
            }
            if (i < n - 1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
            }

            if (execvp(pl->cmds[i].argv[0], pl->cmds[i].argv) == -1)
                perror(pl->cmds[i].argv[0]);
            exit(EXIT_FAILURE);
        }

        /* parent */
        pids[i] = pid;
        if (i < n - 1) {
            close(fd[1]);
            if (infd != STDIN_FILENO) close(infd);
            infd = fd[0];
        } else {
            if (infd != STDIN_FILENO) close(infd);
        }
    }

    if (!pl->background) {
        for (int i = 0; i < n; i++) {
            int status;
            waitpid(pids[i], &status, 0);
        }
    } else {
        printf("[background pipeline] %d commands started\n", n);
    }

    return 0;
}
