/* worker_io_bound - syscall-heavy / compute-light interference generator.
 *
 * Standalone process. Pins itself to one logical CPU and hammers a self-pipe:
 * per iteration it does write(64B) -> read(64B) -> poll(timeout=0), i.e. three
 * syscalls and at least one scheduler yield opportunity, with NO disk
 * dependency whatsoever. This is the workload meant to perturb a co-located
 * simulator thread via scheduler preemption rather than via raw CPU steal.
 *
 * stdout is left completely empty; banner + summary go to stderr.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core_affinity.h"
#include "delay_inject.h"

#define WORKER_NAME     "worker_io_bound"
#define DUTY_PERIOD_NS  10000000ull   /* 10 ms duty-cycle period */
#define NS_PER_SEC      1000000000ull
#define IO_CHUNK        64            /* bytes per pipe round-trip */

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;               /* deliberately NOT SA_RESTART: we want a fast exit */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

/* ------------------------------------------------------------------ options */

typedef struct {
    int      cpu;             /* required */
    uint64_t duration_sec;    /* 0 = until signalled */
    int      intensity;       /* 0..100 */
    int      quiet;
} worker_opts_t;

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s --cpu N [--duration-sec S] [--intensity P] [--quiet]\n"
            "  --cpu N           logical CPU to pin this process to (required)\n"
            "  --duration-sec S  run time in seconds (0 = until SIGTERM/SIGINT, default 0)\n"
            "  --intensity P     duty cycle 0..100 %% of each 10ms period (default 100)\n"
            "  --quiet           suppress the startup banner\n",
            argv0);
}

static int parse_opts(int argc, char **argv, worker_opts_t *o)
{
    static const struct option longopts[] = {
        { "cpu",          required_argument, NULL, 'c' },
        { "duration-sec", required_argument, NULL, 'd' },
        { "intensity",    required_argument, NULL, 'i' },
        { "quiet",        no_argument,       NULL, 'q' },
        { "help",         no_argument,       NULL, 'h' },
        { NULL,           0,                 NULL,  0  }
    };
    int c;

    o->cpu = -1;
    o->duration_sec = 0;
    o->intensity = 100;
    o->quiet = 0;

    while ((c = getopt_long(argc, argv, "c:d:i:qh", longopts, NULL)) != -1) {
        char *end = NULL;
        long v;
        switch (c) {
        case 'c':
            v = strtol(optarg, &end, 10);
            if (end == optarg || *end != '\0' || v < 0 || v > INT_MAX) {
                fprintf(stderr, "%s: bad --cpu '%s'\n", WORKER_NAME, optarg);
                return -1;
            }
            o->cpu = (int)v;
            break;
        case 'd':
            v = strtol(optarg, &end, 10);
            if (end == optarg || *end != '\0' || v < 0) {
                fprintf(stderr, "%s: bad --duration-sec '%s'\n", WORKER_NAME, optarg);
                return -1;
            }
            o->duration_sec = (uint64_t)v;
            break;
        case 'i':
            v = strtol(optarg, &end, 10);
            if (end == optarg || *end != '\0' || v < 0 || v > 100) {
                fprintf(stderr, "%s: bad --intensity '%s' (want 0..100)\n", WORKER_NAME, optarg);
                return -1;
            }
            o->intensity = (int)v;
            break;
        case 'q':
            o->quiet = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 1;
        default:
            usage(argv[0]);
            return -1;
        }
    }
    if (o->cpu < 0) {
        fprintf(stderr, "%s: --cpu is required\n", WORKER_NAME);
        usage(argv[0]);
        return -1;
    }
    return 0;
}

/* --------------------------------------------------------------- duty cycle */

static void idle_until_ns(uint64_t abs_ns)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(abs_ns / NS_PER_SEC);
    ts.tv_nsec = (long)(abs_ns % NS_PER_SEC);
    for (;;) {
        int rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        if (rc == 0 || rc != EINTR || g_stop)
            return;
    }
}

/* ---------------------------------------------------------------- self-pipe */

static int  g_pipe[2] = { -1, -1 };
static char g_wbuf[IO_CHUNK];
static char g_rbuf[IO_CHUNK];

/* write -> read -> poll(0). Returns 0 on success, -1 on a hard error.
 * Adds the number of syscalls actually issued to *syscalls. */
static int io_iteration(uint64_t *syscalls, uint64_t *rt_bytes)
{
    ssize_t n;

    for (;;) {
        n = write(g_pipe[1], g_wbuf, sizeof(g_wbuf));
        (*syscalls)++;
        if (n >= 0)
            break;
        if (errno == EINTR && !g_stop)
            continue;
        return -1;
    }

    size_t want = (size_t)n, got = 0;
    while (got < want) {
        ssize_t r = read(g_pipe[0], g_rbuf + got, want - got);
        (*syscalls)++;
        if (r > 0) {
            got += (size_t)r;
            continue;
        }
        if (r == 0)
            return -1;                      /* writer end gone: cannot happen here */
        if (errno == EINTR && !g_stop)
            continue;
        return -1;
    }
    *rt_bytes += (uint64_t)got;

    struct pollfd pfd;
    pfd.fd      = g_pipe[0];
    pfd.events  = POLLIN;
    pfd.revents = 0;
    (void)poll(&pfd, 1, 0);                 /* 0 ms: a pure yield point */
    (*syscalls)++;

    return 0;
}

/* --------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    worker_opts_t o;

    /* Installed FIRST, before any other work, so the harness can SIGTERM us at
     * any point after exec and still get a clean exit rather than a kill. */
    install_signal_handlers();

    int rc = parse_opts(argc, argv, &o);
    if (rc != 0)
        return rc > 0 ? 0 : 1;

    int pin = ca_pin_process(o.cpu);
    if (pin != 0) {
        fprintf(stderr, "%s: ca_pin_process(%d) failed: %s\n",
                WORKER_NAME, o.cpu, strerror(pin < 0 ? -pin : pin));
        return 1;
    }
    int verified = ca_verify_pinned(o.cpu);
    if (verified != 1) {
        fprintf(stderr, "%s: ca_verify_pinned(%d) failed (rc=%d)\n",
                WORKER_NAME, o.cpu, verified);
        return 1;
    }

    if (pipe2(g_pipe, 0) != 0) {
        fprintf(stderr, "%s: pipe2 failed: %s\n", WORKER_NAME, strerror(errno));
        return 1;
    }
    memset(g_wbuf, 0x5A, sizeof(g_wbuf));

    if (!o.quiet) {
        fprintf(stderr,
                "[%s] pid=%d cpu=%d verified=yes duration=%llus intensity=%d%%\n",
                WORKER_NAME, (int)getpid(), o.cpu,
                (unsigned long long)o.duration_sec, o.intensity);
        fflush(stderr);
    }

    const uint64_t work_ns = DUTY_PERIOD_NS * (uint64_t)o.intensity / 100ull;
    const uint64_t t0 = di_now_ns();
    const uint64_t hard_deadline = o.duration_sec ? t0 + o.duration_sec * NS_PER_SEC : 0;

    uint64_t iters = 0, syscalls = 0, bytes = 0;
    int io_error = 0;

    while (!g_stop && !io_error) {
        const uint64_t period_start = di_now_ns();
        if (hard_deadline && period_start >= hard_deadline)
            break;

        uint64_t work_end = period_start + work_ns;
        if (hard_deadline && work_end > hard_deadline)
            work_end = hard_deadline;

        while (!g_stop && di_now_ns() < work_end) {
            if (io_iteration(&syscalls, &bytes) != 0) {
                if (!g_stop) {
                    fprintf(stderr, "%s: pipe io failed: %s\n",
                            WORKER_NAME, strerror(errno));
                    io_error = 1;
                }
                break;
            }
            iters++;
        }

        if (o.intensity >= 100)
            continue;                       /* never idle */
        uint64_t next = period_start + DUTY_PERIOD_NS;
        if (hard_deadline && next > hard_deadline)
            break;
        idle_until_ns(next);
    }

    const uint64_t elapsed = di_now_ns() - t0;
    close(g_pipe[0]);
    close(g_pipe[1]);

    fprintf(stderr,
            "[%s] summary: iterations=%llu syscalls=%llu bytes=%llu elapsed_ms=%.3f\n",
            WORKER_NAME,
            (unsigned long long)iters,
            (unsigned long long)syscalls,
            (unsigned long long)bytes,
            (double)elapsed / 1e6);
    fflush(stderr);
    return (io_error && !g_stop) ? 1 : 0;
}
