/* worker_mixed_5050 - 50/50 compute + blocking-I/O interference generator.
 *
 * Standalone process. Pins itself to one logical CPU and alternates between the
 * cpu-bound 64x64 matmul and the io-bound self-pipe round-trip, splitting them
 * by WALL TIME (not by iteration count): the nominal quantum is half of a ~2 ms
 * sub-period, i.e. ~1 ms compute then ~1 ms pipe I/O. The compute side advances
 * the matmul one output ROW at a time so a phase switch is fine-grained
 * (~microseconds) and the split is not skewed by a coarse work quantum.
 *
 * Each quantum is DEFICIT-DRIVEN: a phase runs until its cumulative time would
 * reach the other phase's total plus one half-quantum (capped at one sub-period
 * of catch-up). That matters because on a loaded machine a phase routinely
 * overruns its quantum - the process is preempted mid-phase - and because the
 * duty-cycle window can truncate whichever phase is running when it closes.
 * Without the feedback those two effects bias the split by several percent
 * toward compute. The phase also persists ACROSS duty-cycle periods, so neither
 * side gets a structural advantage from always going first.
 *
 * The actual measured compute-vs-io time split is reported at exit so the
 * 50:50 claim in the report is verifiable rather than assumed.
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

#define WORKER_NAME       "worker_mixed_5050"
#define DUTY_PERIOD_NS    10000000ull   /* 10 ms duty-cycle period            */
#define SUB_PERIOD_NS     2000000ull    /* 2 ms compute/io alternation period */
#define SUB_HALF_NS       (SUB_PERIOD_NS / 2)
#define NS_PER_SEC        1000000000ull
#define IO_CHUNK          64

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

/* ------------------------------------------------------- compute: 64x64 mm */

#define MATN 64
static double g_A[MATN][MATN];
static double g_B[MATN][MATN];
static double g_C[MATN][MATN];
static volatile double g_sink;

static uint64_t g_mm_pass = 0;   /* completed full C = A*B passes */
static int      g_mm_row  = 0;   /* next output row to compute    */

static void matmul_init(void)
{
    for (int i = 0; i < MATN; i++) {
        for (int j = 0; j < MATN; j++) {
            g_A[i][j] = (double)((i * 31 + j * 17) % 97) / 97.0 + 0.5;
            g_B[i][j] = (double)((i * 13 + j * 7) % 89) / 89.0 + 0.25;
        }
    }
}

/* Compute ONE output row of C = A*B. Re-seeds one element of A at the start of
 * every pass so the result cannot be hoisted or cached by the optimiser. */
static void matmul_step_row(void)
{
    const int i = g_mm_row;

    if (i == 0)
        g_A[g_mm_pass % MATN][(g_mm_pass / MATN) % MATN] +=
            1e-12 + 1e-15 * (double)(g_mm_pass & 0xFFu);

    double *ci = g_C[i];
    for (int j = 0; j < MATN; j++)
        ci[j] = 0.0;
    for (int k = 0; k < MATN; k++) {
        const double a = g_A[i][k];
        const double *bk = g_B[k];
        for (int j = 0; j < MATN; j++)
            ci[j] += a * bk[j];
    }
    g_sink = ci[(uint64_t)(i * 7) % MATN];

    if (++g_mm_row == MATN) {
        g_mm_row = 0;
        g_mm_pass++;
    }
}

/* ---------------------------------------------------------- io: self-pipe */

static int  g_pipe[2] = { -1, -1 };
static char g_wbuf[IO_CHUNK];
static char g_rbuf[IO_CHUNK];

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
            return -1;
        if (errno == EINTR && !g_stop)
            continue;
        return -1;
    }
    *rt_bytes += (uint64_t)got;

    struct pollfd pfd;
    pfd.fd      = g_pipe[0];
    pfd.events  = POLLIN;
    pfd.revents = 0;
    (void)poll(&pfd, 1, 0);
    (*syscalls)++;

    return 0;
}

/* ------------------------------------------------------------ phase control */

#define PHASE_COMPUTE 0
#define PHASE_IO      1

/* How long the given phase should run next, in ns. Aims to bring that phase's
 * cumulative time up to the other phase's total plus one half-quantum, so any
 * imbalance from preemption or from a truncated duty-cycle window is repaid on
 * the following turns. Returns 0 when this phase is already ahead, which simply
 * hands the turn straight back to the other one. Catch-up is capped at one
 * sub-period so a large deficit drains smoothly instead of in one long burst. */
static uint64_t phase_quantum_ns(int phase, uint64_t compute_ns, uint64_t io_ns)
{
    const uint64_t self  = (phase == PHASE_IO) ? io_ns : compute_ns;
    const uint64_t other = (phase == PHASE_IO) ? compute_ns : io_ns;

    if (other + SUB_HALF_NS <= self)
        return 0;
    const uint64_t want = other + SUB_HALF_NS - self;
    return want > SUB_PERIOD_NS ? SUB_PERIOD_NS : want;
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
    matmul_init();

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

    uint64_t mm_rows = 0, io_iters = 0, syscalls = 0, bytes = 0;
    uint64_t compute_ns = 0, io_ns = 0;
    int io_error = 0;
    int phase = PHASE_COMPUTE;   /* persists across duty-cycle periods */

    while (!g_stop && !io_error) {
        const uint64_t period_start = di_now_ns();
        if (hard_deadline && period_start >= hard_deadline)
            break;

        uint64_t work_end = period_start + work_ns;
        if (hard_deadline && work_end > hard_deadline)
            work_end = hard_deadline;

        while (!g_stop && !io_error) {
            const uint64_t a = di_now_ns();
            if (a >= work_end)
                break;

            uint64_t phase_end = a + phase_quantum_ns(phase, compute_ns, io_ns);
            if (phase_end > work_end)
                phase_end = work_end;

            if (phase == PHASE_COMPUTE) {
                while (!g_stop && di_now_ns() < phase_end) {
                    matmul_step_row();
                    mm_rows++;
                }
            } else {
                while (!g_stop && di_now_ns() < phase_end) {
                    if (io_iteration(&syscalls, &bytes) != 0) {
                        if (!g_stop) {
                            fprintf(stderr, "%s: pipe io failed: %s\n",
                                    WORKER_NAME, strerror(errno));
                            io_error = 1;
                        }
                        break;
                    }
                    io_iters++;
                }
            }

            const uint64_t b = di_now_ns();
            if (phase == PHASE_COMPUTE)
                compute_ns += b - a;
            else
                io_ns += b - a;
            phase = (phase == PHASE_COMPUTE) ? PHASE_IO : PHASE_COMPUTE;
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

    const double busy = (double)(compute_ns + io_ns);
    const double cpct = busy > 0.0 ? 100.0 * (double)compute_ns / busy : 0.0;
    const double ipct = busy > 0.0 ? 100.0 * (double)io_ns / busy : 0.0;

    fprintf(stderr,
            "[%s] summary: matmul_rows=%llu (passes=%llu) io_iterations=%llu "
            "syscalls=%llu bytes=%llu elapsed_ms=%.3f\n",
            WORKER_NAME,
            (unsigned long long)mm_rows,
            (unsigned long long)g_mm_pass,
            (unsigned long long)io_iters,
            (unsigned long long)syscalls,
            (unsigned long long)bytes,
            (double)elapsed / 1e6);
    fprintf(stderr,
            "[%s] split: compute_ms=%.3f io_ms=%.3f -> %.1f:%.1f (target 50.0:50.0)\n",
            WORKER_NAME,
            (double)compute_ns / 1e6, (double)io_ns / 1e6, cpct, ipct);
    fflush(stderr);
    return (io_error && !g_stop) ? 1 : 0;
}
