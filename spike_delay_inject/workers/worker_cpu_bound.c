/* worker_cpu_bound - pure-compute interference generator for the delay-injection spike.
 *
 * Standalone process. Pins itself to one logical CPU and burns it with a 64x64
 * double matrix multiply (~96 KB of working set across A/B/C, so it stays in
 * L1/L2 and isolates *core* contention from cache/memory contention).
 * NO syscalls are issued in the work phase, so a co-located simulator thread is
 * perturbed by raw CPU steal only, not by scheduler churn.
 *
 * stdout is left completely empty; banner + summary go to stderr.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "core_affinity.h"
#include "delay_inject.h"

#define WORKER_NAME     "worker_cpu_bound"
#define DUTY_PERIOD_NS  10000000ull   /* 10 ms duty-cycle period */
#define NS_PER_SEC      1000000000ull

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

/* Idle until the absolute CLOCK_MONOTONIC deadline, restarting on EINTR unless
 * we have been asked to stop. */
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

/* ------------------------------------------------------------------- matmul */

#define MATN 64
static double g_A[MATN][MATN];
static double g_B[MATN][MATN];
static double g_C[MATN][MATN];
static volatile double g_sink;   /* keeps the compiler from deleting the work */

static void matmul_init(void)
{
    for (int i = 0; i < MATN; i++) {
        for (int j = 0; j < MATN; j++) {
            g_A[i][j] = (double)((i * 31 + j * 17) % 97) / 97.0 + 0.5;
            g_B[i][j] = (double)((i * 13 + j * 7) % 89) / 89.0 + 0.25;
        }
    }
}

/* One full C = A*B pass. Re-seeds one element of A first so nothing can be
 * hoisted out of the loop. */
static void matmul_pass(uint64_t pass)
{
    g_A[pass % MATN][(pass / MATN) % MATN] += 1e-12 + 1e-15 * (double)(pass & 0xFFu);

    for (int i = 0; i < MATN; i++) {
        double *ci = g_C[i];
        for (int j = 0; j < MATN; j++)
            ci[j] = 0.0;
        for (int k = 0; k < MATN; k++) {
            const double a = g_A[i][k];
            const double *bk = g_B[k];
            for (int j = 0; j < MATN; j++)
                ci[j] += a * bk[j];
        }
    }
    g_sink = g_C[pass % MATN][(pass * 7u) % MATN];
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

    if (!o.quiet) {
        fprintf(stderr,
                "[%s] pid=%d cpu=%d verified=yes duration=%llus intensity=%d%%\n",
                WORKER_NAME, (int)getpid(), o.cpu,
                (unsigned long long)o.duration_sec, o.intensity);
        fflush(stderr);
    }

    matmul_init();

    const uint64_t work_ns = DUTY_PERIOD_NS * (uint64_t)o.intensity / 100ull;
    const uint64_t t0 = di_now_ns();
    const uint64_t hard_deadline = o.duration_sec ? t0 + o.duration_sec * NS_PER_SEC : 0;

    uint64_t passes = 0;

    while (!g_stop) {
        const uint64_t period_start = di_now_ns();
        if (hard_deadline && period_start >= hard_deadline)
            break;

        uint64_t work_end = period_start + work_ns;
        if (hard_deadline && work_end > hard_deadline)
            work_end = hard_deadline;

        while (!g_stop && di_now_ns() < work_end) {
            matmul_pass(passes);
            passes++;
        }

        if (o.intensity >= 100)
            continue;                       /* never idle */
        uint64_t next = period_start + DUTY_PERIOD_NS;
        if (hard_deadline && next > hard_deadline)
            break;
        idle_until_ns(next);
    }

    const uint64_t elapsed = di_now_ns() - t0;
    fprintf(stderr, "[%s] summary: matmul_passes=%llu flops=%llu elapsed_ms=%.3f sink=%g\n",
            WORKER_NAME,
            (unsigned long long)passes,
            (unsigned long long)(passes * (uint64_t)MATN * MATN * MATN * 2ull),
            (double)elapsed / 1e6,
            (double)g_sink);
    fflush(stderr);
    return 0;
}
