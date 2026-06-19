/*
 * psh.c — Pico Shell with Command Anatomy (Week 3 + Chapter 2 refactor)
 *
 * Architecture:
 *   - Every built-in command is a cmd_spec_t module (cmd_quit.c, cmd_jobs.c,
 *     cmd_bgfg.c, cmd_help.c).  Each module owns its own argument parsing
 *     (argtable2/3) and help text.
 *   - At startup, all specs are registered with the in-memory registry
 *     (registry.c).
 *   - eval() calls registry_find() first.  If the command is registered,
 *     it calls spec->run(argc, argv).  Otherwise it forks + execve.
 *   - Signal handling (SIGCHLD, SIGINT, SIGTSTP) is unchanged from Week 3.
 *   - Two extern functions expose the job list to the command modules:
 *       listjobs_external()  — used by cmd_jobs.c
 *       do_bgfg_external()   — used by cmd_bgfg.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "cmd_spec.h"

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */
#define MAXLINE   1024
#define MAXARGS    128
#define MAXJOBS     16
#define MAXJID   1<<16

/* Job states */
#define UNDEF 0
#define FG    1
#define BG    2
#define ST    3

/* -----------------------------------------------------------------------
 * Job list
 * ----------------------------------------------------------------------- */
struct job_t {
    pid_t pid;
    int   jid;
    int   state;
    char  cmdline[MAXLINE];
};

extern char **environ;
char prompt[]  = "psh> ";
int  verbose   = 0;
int  nextjid   = 1;
char sbuf[MAXLINE];

struct job_t jobs[MAXJOBS];

/* -----------------------------------------------------------------------
 * Forward declarations — internal helpers
 * ----------------------------------------------------------------------- */
int  parseline(const char *cmdline, char **argv);
void waitfg(pid_t pid);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int  maxjid(struct job_t *jobs);
int  addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int  deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int  pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);

/* -----------------------------------------------------------------------
 * External command specs (defined in their respective cmd_*.c files)
 * ----------------------------------------------------------------------- */
extern const cmd_spec_t cmd_quit_spec;
extern const cmd_spec_t cmd_jobs_spec;
extern const cmd_spec_t cmd_bg_spec;
extern const cmd_spec_t cmd_fg_spec;
extern const cmd_spec_t cmd_help_spec;

/* -----------------------------------------------------------------------
 * listjobs_external — called by cmd_jobs.c
 * Wrapper so the command module can print jobs without knowing internals.
 * ----------------------------------------------------------------------- */
void listjobs_external(FILE *out)
{
    int i;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            fprintf(out, "[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
            case BG: fprintf(out, "Running  "); break;
            case FG: fprintf(out, "Foreground "); break;
            case ST: fprintf(out, "Stopped  "); break;
            default:
                fprintf(out, "listjobs: Internal error: job[%d].state=%d ",
                        i, jobs[i].state);
            }
            fprintf(out, "%s", jobs[i].cmdline);
        }
    }
}

/* -----------------------------------------------------------------------
 * do_bgfg_external — called by cmd_bgfg.c
 * Implements the bg/fg logic using the job list.
 * argv[0] = "bg" or "fg", argv[1] = "%jid" or "pid"
 * ----------------------------------------------------------------------- */
void do_bgfg_external(char **argv)
{
    struct job_t *job = NULL;
    char         *id  = argv[1];
    int           jid;
    pid_t         pid;

    if (id == NULL) {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    if (id[0] == '%') {
        jid = atoi(&id[1]);
        job = getjobjid(jobs, jid);
        if (job == NULL) {
            printf("%s: No such job\n", id);
            return;
        }
    } else if (isdigit(id[0])) {
        pid = atoi(id);
        job = getjobpid(jobs, pid);
        if (job == NULL) {
            printf("(%d): No such process\n", pid);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    kill(-(job->pid), SIGCONT);

    if (!strcmp(argv[0], "bg")) {
        job->state = BG;
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    } else {
        job->state = FG;
        waitfg(job->pid);
    }
}

/* -----------------------------------------------------------------------
 * eval — main command dispatcher
 *
 * 1. parseline() splits input into argv[]
 * 2. registry_find() checks for a registered built-in
 *    → if found: call spec->run(argc, argv) directly (no fork)
 * 3. Otherwise: fork + execve (external program)
 * ----------------------------------------------------------------------- */
void eval(char *cmdline)
{
    char     *argv[MAXARGS];
    char      buf[MAXLINE];
    int       bg;
    pid_t     pid;
    sigset_t  mask;
    int       argc;
    const cmd_spec_t *spec;

    strcpy(buf, cmdline);
    bg = parseline(buf, argv);

    if (argv[0] == NULL)
        return; /* blank line */

    /*
     * Count argc for the command modules.
     * parseline() null-terminates argv but doesn't return argc.
     */
    for (argc = 0; argv[argc] != NULL; argc++)
        ;

    /* ----------------------------------------------------------------
     * Registry dispatch — built-in commands via cmd_spec_t
     * ---------------------------------------------------------------- */
    spec = registry_find(argv[0]);
    if (spec != NULL) {
        spec->run(argc, argv);
        return;
    }

    /* ----------------------------------------------------------------
     * External program — fork + execve
     * ---------------------------------------------------------------- */
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    if ((pid = fork()) < 0)
        unix_error("fork error");

    if (pid == 0) {
        /* Child */
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
        setpgid(0, 0);
        if (execve(argv[0], argv, environ) < 0) {
            printf("%s: Command not found.\n", argv[0]);
            exit(0);
        }
    }

    /* Parent */
    if (!bg)
        addjob(jobs, pid, FG, cmdline);
    else
        addjob(jobs, pid, BG, cmdline);

    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    if (!bg)
        waitfg(pid);
    else
        printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int  emit_prompt = 1;

    dup2(1, 2); /* redirect stderr → stdout */

    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h': usage();      break;
        case 'v': verbose = 1;  break;
        case 'p': emit_prompt = 0; break;
        default:  usage();
        }
    }

    /* Install signal handlers */
    Signal(SIGINT,  sigint_handler);
    Signal(SIGTSTP, sigtstp_handler);
    Signal(SIGCHLD, sigchld_handler);
    Signal(SIGQUIT, sigquit_handler);

    /* Initialise job list */
    initjobs(jobs);

    /* ----------------------------------------------------------------
     * Register all built-in command specs with the registry.
     * Add new commands here as the shell grows.
     * ---------------------------------------------------------------- */
    registry_register(&cmd_quit_spec);
    registry_register(&cmd_jobs_spec);
    registry_register(&cmd_bg_spec);
    registry_register(&cmd_fg_spec);
    registry_register(&cmd_help_spec);

    /* Read/eval loop */
    while (1) {
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) {
            fflush(stdout);
            exit(0);
        }
        eval(cmdline);
        fflush(stdout);
    }

    exit(0);
}

/* -----------------------------------------------------------------------
 * parseline — split raw input into argv[], detect trailing &
 * ----------------------------------------------------------------------- */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE];
    char *buf   = array;
    char *delim;
    int   argc;
    int   bg;

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';
    while (*buf && (*buf == ' '))
        buf++;

    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    } else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' '))
            buf++;
        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        } else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0)
        return 1;

    if ((bg = (*argv[argc-1] == '&')) != 0)
        argv[--argc] = NULL;

    return bg;
}

/* -----------------------------------------------------------------------
 * waitfg — sleep until pid is no longer the foreground job
 * ----------------------------------------------------------------------- */
void waitfg(pid_t pid)
{
    while (fgpid(jobs) == pid)
        sleep(1);
}

/* -----------------------------------------------------------------------
 * Signal handlers
 * ----------------------------------------------------------------------- */
void sigchld_handler(int sig)
{
    (void)sig;
    int           status;
    pid_t         pid;
    struct job_t *job;

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        if (WIFEXITED(status)) {
            deletejob(jobs, pid);
        } else if (WIFSIGNALED(status)) {
            printf("Job [%d] (%d) terminated by signal: %s\n",
                   pid2jid(pid), pid, strsignal(WTERMSIG(status)));
            deletejob(jobs, pid);
        } else if (WIFSTOPPED(status)) {
            job = getjobpid(jobs, pid);
            if (job != NULL) {
                job->state = ST;
                printf("Job [%d] (%d) stopped by signal: %s\n",
                       job->jid, pid, strsignal(WSTOPSIG(status)));
            }
        }
    }
}

void sigint_handler(int sig)
{
    (void)sig;
    pid_t pid = fgpid(jobs);
    if (pid != 0)
        kill(-pid, SIGINT);
}

void sigtstp_handler(int sig)
{
    (void)sig;
    pid_t pid = fgpid(jobs);
    if (pid != 0)
        kill(-pid, SIGTSTP);
}

void sigquit_handler(int sig)
{
    (void)sig;
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

/* -----------------------------------------------------------------------
 * Job list helpers (unchanged from Week 3)
 * ----------------------------------------------------------------------- */
void clearjob(struct job_t *job) {
    job->pid = 0; job->jid = 0;
    job->state = UNDEF; job->cmdline[0] = '\0';
}

void initjobs(struct job_t *jobs) {
    int i;
    for (i = 0; i < MAXJOBS; i++) clearjob(&jobs[i]);
}

int maxjid(struct job_t *jobs) {
    int i, max = 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max) max = jobs[i].jid;
    return max;
}

int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;
    if (pid < 1) return 0;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS) nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose)
                printf("Added job [%d] %d %s\n",
                       jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

int deletejob(struct job_t *jobs, pid_t pid) {
    int i;
    if (pid < 1) return 0;
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

pid_t fgpid(struct job_t *jobs) {
    int i;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG) return jobs[i].pid;
    return 0;
}

struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;
    if (pid < 1) return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) return &jobs[i];
    return NULL;
}

struct job_t *getjobjid(struct job_t *jobs, int jid) {
    int i;
    if (jid < 1) return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid) return &jobs[i];
    return NULL;
}

int pid2jid(pid_t pid) {
    int i;
    if (pid < 1) return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) return jobs[i].jid;
    return 0;
}

void listjobs(struct job_t *j) {
    (void)j;
    listjobs_external(stdout);
}

/* -----------------------------------------------------------------------
 * Other helpers
 * ----------------------------------------------------------------------- */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}
