/* qemu_mainloop_demo.c - standalone reproduction of the QEMU main-loop millisecond rounding,
 * and a benchmark of qemu_co_sleep_ns() accuracy under different host conditions. No guest.
 *
 * N coroutines (default 3) on QEMU's main AioContext each call
 * qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, req) repeatedly (default 50,000 times per delay) and
 * measure how long each call really took. Several at once is what eVSSIM does when several
 * devices have I/O in flight: their sleeps overlap on the one main-loop thread.
 *
 *   --driver main_loop  main_loop_wait(), QEMU's real main loop. The AioContext timer reaches
 *                       it through a glib source whose prepare() converts the deadline with
 *                       qemu_timeout_ns_to_ms() = DIV_ROUND_UP(ns, 1 ms): with the stock
 *                       util/main-loop.c every sleep under 1 ms becomes 1 ms. This is the
 *                       path eVSSIM's I/O coroutines use.
 *   --driver aio_poll   aio_poll(ctx, true) directly: ppoll() at nanosecond resolution. What
 *                       the spike's first standalone harness did, and why it missed the bug.
 *
 * scripts/build_demo.sh links this against a STOCK and a PATCHED util/main-loop.c
 * (scripts/qemu_main_loop_ns_deadline.patch); --variant just labels the output.
 *
 * Output: a table on stdout and, with --raw, a CSV in the spike's raw format
 * (one row per sleep) that scripts/check_tolerance.py and scripts/demo_plots.py read.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/aio.h"
#include "qemu/main-loop.h"
#include "qemu/coroutine.h"
#include "qemu/timer.h"

#include <getopt.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/resource.h>

#define MAX_DELAYS 32
#define MAX_COS    64
#define WARMUP     50

static int64_t g_delays_ns[MAX_DELAYS];
static int     g_ndelays;
static long    g_samples = 50000;
static int     g_ncos = 3;
static int     g_use_main_loop = 1;
static int     g_pin_cpu = -1;
/* mechanism: 0 = qemu_co_sleep_ns for the whole delay; 1 = hybrid: co_sleep to target-slack, then
 * busy-spin on get_clock() INSIDE the coroutine (blocks the main loop for up to slack);
 * 2 = spin: busy-spin the whole delay in the coroutine (blocks the main loop for the whole delay) */
static int     g_mech = 0;
static int64_t g_slack_ns = 20000;
static int64_t g_wall_start[MAX_DELAYS], g_wall_end[MAX_DELAYS];
static double  g_cpu_start[MAX_DELAYS], g_cpu_end[MAX_DELAYS];
static int     g_started[MAX_DELAYS], g_ended[MAX_DELAYS];
static int     g_finished;
static const char *g_variant = "unknown", *g_raw = NULL, *g_label = "demo",
                  *g_scenario = "B", *g_isolation = "none", *g_colocation = "none",
                  *g_iftype = "none";

/* results[co][delay]: samples in acquisition order; sorted only in print_results() */
static int64_t *g_res[MAX_COS][MAX_DELAYS];

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static int64_t pct(int64_t *s, long n, double p)
{
    long i = (long)(p * n);
    if (i >= n) i = n - 1;
    return s[i];
}

static double cpu_seconds(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
}

static inline void coroutine_fn do_delay(int64_t req)
{
    int64_t target = get_clock() + req;
    if (g_mech == 0) {
        qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, req);
        return;
    }
    if (g_mech == 1 && req > g_slack_ns)
        qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, req - g_slack_ns);
    while (get_clock() < target)
        __asm__ __volatile__("pause" ::: "memory");
}

static void coroutine_fn demo_coroutine(void *opaque)
{
    int idx = (int)(intptr_t)opaque;
    int di;

    for (di = 0; di < g_ndelays; di++) {
        const int64_t req = g_delays_ns[di];
        int64_t *buf = g_malloc(sizeof(int64_t) * g_samples);
        long s;

        for (s = 0; s < WARMUP; s++)
            do_delay(req);
        if (!g_started[di]) { g_started[di] = 1; g_wall_start[di] = get_clock(); g_cpu_start[di] = cpu_seconds(); }
        for (s = 0; s < g_samples; s++) {
            int64_t t0 = get_clock();
            do_delay(req);
            buf[s] = get_clock() - t0;
        }
        if (++g_ended[di] == g_ncos) { g_wall_end[di] = get_clock(); g_cpu_end[di] = cpu_seconds(); }
        /* Not sorted here: a qsort of 50,000 samples takes milliseconds on the main-loop
         * thread and would show up as late wake-ups in the OTHER coroutines. */
        g_res[idx][di] = buf;
    }
    g_finished++;
}

static void write_raw(void)
{
    FILE *f = fopen(g_raw, "w");
    char backend[64];
    long tid = syscall(SYS_gettid);
    int di, c;

    if (!f) { perror(g_raw); return; }
    snprintf(backend, sizeof backend, "%s/%s/%s", g_variant, g_use_main_loop ? "main_loop" : "aio_poll",
             g_mech == 0 ? "co_sleep" : g_mech == 1 ? "hybrid" : "spin");
    fputs("run_label,scenario,isolation,colocation,iftype,backend,thread_idx,tid,"
          "pinned_cpu,phys_core,smt_sibling,requested_ns,actual_ns,delta_ns,seq\n", f);
    for (di = 0; di < g_ndelays; di++) {
        const int64_t req = g_delays_ns[di];
        for (c = 0; c < g_ncos; c++) {
            long s;
            for (s = 0; s < g_samples; s++) {
                int64_t act = g_res[c][di][s];
                fprintf(f, "%s,%s,%s,%s,%s,%s,%d,%ld,%d,-1,-1,%" PRId64 ",%" PRId64 ",%" PRId64 ",%ld\n",
                        g_label, g_scenario, g_isolation, g_colocation, g_iftype, backend,
                        c, tid, g_pin_cpu, req, act, act - req, s);
            }
        }
    }
    fclose(f);
}

static void print_results(void)
{
    const char *drv = g_use_main_loop ? "main_loop" : "aio_poll";
    int di, c;

    printf("%-8s %-10s %3s %9s %12s %12s %12s %12s %12s\n",
           "variant", "driver", "co", "requested", "actual p50", "actual p99", "delta p50", "delta p99", "max");
    for (di = 0; di < g_ndelays; di++) {
        const int64_t req = g_delays_ns[di];
        long n_all = g_samples * g_ncos;
        int64_t *all = g_malloc(sizeof(int64_t) * n_all);
        for (c = 0; c < g_ncos; c++) {
            int64_t *buf = g_res[c][di];
            memcpy(all + (size_t)c * g_samples, buf, sizeof(int64_t) * g_samples);
            qsort(buf, g_samples, sizeof(int64_t), cmp_i64);
            printf("%-8s %-10s %3d %6.0f us %9.1f us %9.1f us %+9.1f us %+9.1f us %9.1f us\n",
                   g_variant, drv, c, req / 1e3,
                   pct(buf, g_samples, .5) / 1e3, pct(buf, g_samples, .99) / 1e3,
                   (pct(buf, g_samples, .5) - req) / 1e3, (pct(buf, g_samples, .99) - req) / 1e3,
                   buf[g_samples - 1] / 1e3);
        }
        qsort(all, n_all, sizeof(int64_t), cmp_i64);
        printf("%-8s %-10s %3s %6.0f us %9.1f us %9.1f us %+9.1f us %+9.1f us %9.1f us  (n=%ld)\n",
               g_variant, drv, "all", req / 1e3,
               pct(all, n_all, .5) / 1e3, pct(all, n_all, .99) / 1e3,
               (pct(all, n_all, .5) - req) / 1e3, (pct(all, n_all, .99) - req) / 1e3,
               all[n_all - 1] / 1e3, n_all);
        {
            double wall = (g_wall_end[di] - g_wall_start[di]) / 1e9;
            double ideal = (double)g_samples * req / 1e9;   /* if all coroutines overlap perfectly */
            printf("%-8s %-10s RUN %6.0f us  wall %.2f s (ideal %.2f s, x%.2f)  cpu %.2f s (%.0f%% of wall)  mech=%s slack=%" PRId64 "us\n",
                   g_variant, drv, req / 1e3, wall, ideal, wall / ideal,
                   g_cpu_end[di] - g_cpu_start[di], 100.0 * (g_cpu_end[di] - g_cpu_start[di]) / wall,
                   g_mech == 0 ? "co_sleep" : g_mech == 1 ? "hybrid" : "spin", g_slack_ns / 1000);
        }
        g_free(all);
    }
}

static void usage(void)
{
    fprintf(stderr,
        "usage: qemu_mainloop_demo [--driver main_loop|aio_poll] [--delays-us 132,982]\n"
        "         [--samples N (per coroutine, default 50000)] [--coroutines N (default 3, max %d)]\n"
        "         [--pin-cpu N]  pin the main-loop thread (all coroutines) to logical cpu N\n"
        "         [--variant stock|patched] [--raw FILE.csv] [--run-label L] [--scenario A|B]\n"
        "         [--isolation none|soft|hard] [--colocation C] [--iftype T]\n"
        "         [--mechanism co_sleep|hybrid|spin] [--slack-us N (hybrid, default 20)]\n", MAX_COS);
}

int main(int argc, char **argv)
{
    static const struct option opts[] = {
        { "driver",     required_argument, NULL, 'd' },
        { "delays-us",  required_argument, NULL, 'D' },
        { "samples",    required_argument, NULL, 'n' },
        { "coroutines", required_argument, NULL, 'c' },
        { "pin-cpu",    required_argument, NULL, 'p' },
        { "variant",    required_argument, NULL, 'v' },
        { "raw",        required_argument, NULL, 'r' },
        { "run-label",  required_argument, NULL, 'L' },
        { "scenario",   required_argument, NULL, 'S' },
        { "isolation",  required_argument, NULL, 'I' },
        { "colocation", required_argument, NULL, 'C' },
        { "iftype",     required_argument, NULL, 'T' },
        { "mechanism",  required_argument, NULL, 'm' },
        { "slack-us",   required_argument, NULL, 's' },
        { NULL, 0, NULL, 0 }
    };
    const char *delays = "132,982";
    int c;

    while ((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (c) {
        case 'd':
            if (!strcmp(optarg, "main_loop"))     g_use_main_loop = 1;
            else if (!strcmp(optarg, "aio_poll")) g_use_main_loop = 0;
            else { usage(); return 2; }
            break;
        case 'D': delays = optarg; break;
        case 'n': g_samples = atol(optarg); break;
        case 'c': g_ncos = atoi(optarg);
                  if (g_ncos < 1 || g_ncos > MAX_COS) { usage(); return 2; }
                  break;
        case 'p': g_pin_cpu = atoi(optarg); break;
        case 'v': g_variant = optarg; break;
        case 'r': g_raw = optarg; break;
        case 'L': g_label = optarg; break;
        case 'S': g_scenario = optarg; break;
        case 'I': g_isolation = optarg; break;
        case 'C': g_colocation = optarg; break;
        case 'T': g_iftype = optarg; break;
        case 'm': if (!strcmp(optarg, "co_sleep")) g_mech = 0; else if (!strcmp(optarg, "hybrid")) g_mech = 1;
                  else if (!strcmp(optarg, "spin")) g_mech = 2; else { usage(); return 2; }
                  break;
        case 's': g_slack_ns = atoll(optarg) * 1000LL; break;
        default: usage(); return 2;
        }
    }
    {
        char *tmp = g_strdup(delays), *save = NULL, *tok;
        for (tok = strtok_r(tmp, ",", &save); tok && g_ndelays < MAX_DELAYS;
             tok = strtok_r(NULL, ",", &save))
            g_delays_ns[g_ndelays++] = atoll(tok) * 1000LL;
        g_free(tmp);
    }

    if (g_pin_cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(g_pin_cpu, &set);
        if (sched_setaffinity(0, sizeof set, &set) != 0) {
            perror("sched_setaffinity");
            return 2;
        }
        fprintf(stderr, "pinned to cpu %d (now on cpu %d)\n", g_pin_cpu, sched_getcpu());
    }

    /* Real QEMU set-up: creates the main AioContext, attaches it to the glib
     * default context, and (in init_clocks) sets timer slack to 1 ns. */
    qemu_init_main_loop(&error_fatal);

    /* All coroutines run on this one thread; each enter() runs until its first sleep. */
    for (c = 0; c < g_ncos; c++)
        qemu_coroutine_enter(qemu_coroutine_create(demo_coroutine, (void *)(intptr_t)c));

    if (g_use_main_loop) {
        while (g_finished < g_ncos)
            main_loop_wait(false);
    } else {
        while (g_finished < g_ncos)
            aio_poll(qemu_get_aio_context(), true);
    }
    if (g_raw)
        write_raw();
    print_results();
    return 0;
}
