#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cmd_spec.h"
#include "cmd_threads.h"
#include "json_utils.h"

#define MAX_WORKERS 64

struct worker_args {
    int index;
    int sleep_ms;
    long result;
    pthread_t tid;
};

static long shared_sum = 0;
static pthread_mutex_t sum_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *worker_func(void *arg)
{
    struct worker_args *wa = (struct worker_args *)arg;

    if (wa->sleep_ms > 0) {
        struct timespec ts;
        ts.tv_sec  = wa->sleep_ms / 1000;
        ts.tv_nsec = (wa->sleep_ms % 1000) * 1000000L;
        nanosleep(&ts, NULL);
    }

    long v = (long)(wa->index + 1);
    wa->result = v * v;

    pthread_mutex_lock(&sum_mutex);
    shared_sum += wa->result;
    pthread_mutex_unlock(&sum_mutex);

    return NULL;
}

int threads_run(int argc, char **argv)
{
    int nthreads = 4;
    int sleep_ms = 0;
    int json = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            threads_print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--count") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "threads: %s requires an argument\n", argv[i]);
                return 1;
            }
            nthreads = atoi(argv[++i]);
            if (nthreads < 1 || nthreads > MAX_WORKERS) {
                fprintf(stderr, "threads: count must be 1-%d\n", MAX_WORKERS);
                return 1;
            }
        } else if (strcmp(argv[i], "--sleep") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "threads: --sleep requires an argument\n");
                return 1;
            }
            sleep_ms = atoi(argv[++i]);
            if (sleep_ms < 0 || sleep_ms > 60000) {
                fprintf(stderr, "threads: sleep must be 0-60000 ms\n");
                return 1;
            }
        } else {
            fprintf(stderr, "threads: invalid option: %s\n", argv[i]);
            threads_print_usage(stderr);
            return 1;
        }
    }

    shared_sum = 0;

    struct worker_args workers[MAX_WORKERS];
    for (i = 0; i < nthreads; i++) {
        workers[i].index    = i;
        workers[i].sleep_ms = sleep_ms;
        workers[i].result   = 0;
    }

    if (!json) printf("started %d threads\n", nthreads);

    for (i = 0; i < nthreads; i++) {
        if (pthread_create(&workers[i].tid, NULL, worker_func, &workers[i]) != 0) {
            fprintf(stderr, "threads: failed to create thread %d\n", i);
            return 1;
        }
    }

    for (i = 0; i < nthreads; i++) {
        pthread_join(workers[i].tid, NULL);
    }

    if (json) {
        printf("{\"threads\":%d,\"sleep_ms\":%d,\"sum\":%ld,\"results\":[\n",
               nthreads, sleep_ms, shared_sum);
        for (i = 0; i < nthreads; i++) {
            if (i > 0) printf(",\n");
            printf("  {\"worker\":%d,\"thread_id\":%lu,\"result\":%ld}",
                   workers[i].index + 1,
                   (unsigned long)workers[i].tid,
                   workers[i].result);
        }
        printf("\n]}\n");
    } else {
        for (i = 0; i < nthreads; i++) {
            printf("thread %d id %lu result %ld\n",
                   workers[i].index + 1,
                   (unsigned long)workers[i].tid,
                   workers[i].result);
        }
        printf("sum %ld\n", shared_sum);
    }

    return 0;
}

void threads_print_usage(FILE *out)
{
    fprintf(out, "Usage: threads [-n COUNT] [--sleep MS] [--json] [-h]\n");
    fprintf(out, "\nDescription:\n");
    fprintf(out, "  POSIX thread demo: spawn N workers, each computes index^2.\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  %-20s %s\n", "-h, --help", "show help and exit");
    fprintf(out, "  %-20s %s\n", "-n, --count N", "number of threads (1-64, default 4)");
    fprintf(out, "  %-20s %s\n", "--sleep MS", "milliseconds each thread sleeps (0-60000)");
    fprintf(out, "  %-20s %s\n", "--json", "output in JSON format");
}

static cmd_spec_t cmd_threads_spec = {
    .name        = "threads",
    .summary     = "POSIX thread demo",
    .long_help   = "Spawn N worker threads, each computing index^2, summed via mutex.",
    .run         = threads_run,
    .print_usage = threads_print_usage,
};

void register_threads_command(void)
{
    register_command(&cmd_threads_spec);
}
