/*
 * rt_delay mechanism and cost benchmark: 3 and 16 ucontext coroutines share one thread, as device
 * coroutines share QEMU's main loop. Run on a quiet host, as root to block deep C-states
 * (CPU_IDLE_HOLD=0 to allow them).
 *
 * usage: bench_rt_delay [iters]   (default 5000)
 */
#define _GNU_SOURCE
#include "rt_delay.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define MAX_COS    16
#define STACK_SIZE (64 * 1024)

struct coroutine {
    ucontext_t uc;
    void *stack;
    uint64_t wake_ns;
    int done;
};

static struct {
    ucontext_t main_uc;
    struct coroutine cos[MAX_COS];
    int ncos;
    int cur;
    rtd_ctx *ctx;
    uint64_t delay_us;
    int iters;
} sch;

static int failures;

/* The kernel honours the request only while the fd stays open. */
static int hold_cpu_idle(void)
{
    const char *env = getenv("CPU_IDLE_HOLD");
    int32_t zero = 0;
    int fd;

    if (env && env[0] == '0' && env[1] == '\0') {
        printf("cpuidle: deep C-states allowed (CPU_IDLE_HOLD=0)\n");
        return -1;
    }
    fd = open("/dev/cpu_dma_latency", O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, &zero, sizeof(zero)) != (ssize_t)sizeof(zero)) {
        printf("cpuidle: WARNING could not hold /dev/cpu_dma_latency=0 (%s; needs root) - "
               "C-state exit latency is included in the results\n", strerror(errno));
        if (fd >= 0)
            close(fd);
        return -1;
    }
    printf("cpuidle: deep C-states blocked for the benchmark (cpu_dma_latency=0)\n");
    return fd;
}

static void sleep_until(uint64_t ns)
{
    struct timespec ts = { (time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull) };

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

/* sleep_fn: park the running coroutine and return to the scheduler. */
static void co_sleep(uint64_t ns, void *opaque)
{
    struct coroutine *co = &sch.cos[sch.cur];

    (void)opaque;
    co->wake_ns = rtd_now_ns() + ns;
    swapcontext(&co->uc, &sch.main_uc);
}

static void co_body(void)
{
    int id = sch.cur, i;

    for (i = 0; i < sch.iters; i++) {
        rtd_add_us(sch.ctx, (unsigned)id, sch.delay_us);
        rtd_pause(sch.ctx, (unsigned)id);
    }
    sch.cos[id].done = 1;
}

/* Returns the wall time in ns. */
static uint64_t run_loop(int ncos)
{
    uint64_t t0 = rtd_now_ns();
    int i, left = ncos;

    sch.ncos = ncos;
    for (i = 0; i < ncos; i++) {
        struct coroutine *co = &sch.cos[i];

        co->done = 0;
        co->wake_ns = 0;
        getcontext(&co->uc);
        co->uc.uc_stack.ss_sp = co->stack;
        co->uc.uc_stack.ss_size = STACK_SIZE;
        co->uc.uc_link = &sch.main_uc;
        makecontext(&co->uc, co_body, 0);
    }
    while (left) {
        int next = -1;

        for (i = 0; i < ncos; i++)
            if (!sch.cos[i].done && (next < 0 || sch.cos[i].wake_ns < sch.cos[next].wake_ns))
                next = i;
        if (sch.cos[next].wake_ns > rtd_now_ns())
            sleep_until(sch.cos[next].wake_ns);
        sch.cur = next;
        swapcontext(&sch.main_uc, &sch.cos[next].uc);
        if (sch.cos[next].done)
            left--;
    }
    return rtd_now_ns() - t0;
}

static void run_mechanism(int ncos, uint64_t delay_us, uint64_t slack_ns, int iters)
{
    rtd_config cfg;
    uint64_t wall, ideal = (uint64_t)iters * delay_us * 1000u, worst_median = 0;
    double worst_mean = 0.0, in_band = 0.0, wall_ratio;
    uint64_t waits = 0, oob = 0, late = 0, max = 0;
    int i, hybrid = slack_ns > 0;

    rtd_config_default(&cfg);
    cfg.slack_ns = slack_ns;
    cfg.sleep_fn = co_sleep;
    sch.ctx = rtd_init((unsigned)ncos, &cfg);
    sch.delay_us = delay_us;
    sch.iters = iters;

    wall = run_loop(ncos);

    for (i = 0; i < ncos; i++) {
        rtd_stats st;

        rtd_get_stats(sch.ctx, (unsigned)i, &st);
        if (st.median_over_ns > worst_median)
            worst_median = st.median_over_ns;
        if (st.mean_over_ns > worst_mean)
            worst_mean = st.mean_over_ns;
        if (st.max_over_ns > max)
            max = st.max_over_ns;
        waits += st.waits;
        oob += st.out_of_band;
        late += st.late;
        if (st.requested_ns != ideal) {
            printf("  FAIL slot %d waited %llu ns, added %llu ns\n", i,
                   (unsigned long long)st.requested_ns, (unsigned long long)ideal);
            failures++;
        }
    }
    in_band = waits ? 100.0 * (double)(waits - oob) / (double)waits : 0.0;
    wall_ratio = (double)wall / (double)ideal;
    printf("%-6s %2d slots %4llu us: median %6.2f us  mean %6.2f us  max %8.1f us  "
           "in band %6.2f %%  late %llu  wall %.3f s (x%.4f)\n",
           hybrid ? "hybrid" : "plain", ncos, (unsigned long long)delay_us,
           worst_median / 1e3, worst_mean / 1e3, max / 1e3, in_band,
           (unsigned long long)late, wall / 1e9, wall_ratio);
    if (hybrid) {
        if (worst_median >= 500 || worst_mean >= 1500.0) {
            printf("  FAIL hybrid median must be < 0.5 us and mean < 1.5 us\n");
            failures++;
        }
        if (wall_ratio > 1.01) {
            printf("  FAIL wall time more than 1 %% over the ideal\n");
            failures++;
        }
    }
    rtd_destroy(sch.ctx);
}

static void run_costs(void)
{
    const int adds = 5000000, pauses = 200000;
    const uint64_t req_ns = 1000;   /* inside the spin window: no sleep */
    rtd_ctx *ctx = rtd_init(1, NULL);
    uint64_t t0, add_ns, pause_ns;
    int i;

    t0 = rtd_now_ns();
    for (i = 0; i < adds; i++)
        rtd_add_ns(ctx, 0, 1);
    add_ns = rtd_now_ns() - t0;
    rtd_pause(ctx, 0);   /* drain, and keep per-thread set-up out of the timed loop */

    t0 = rtd_now_ns();
    for (i = 0; i < pauses; i++) {
        rtd_add_ns(ctx, 0, req_ns);
        rtd_pause(ctx, 0);
    }
    pause_ns = rtd_now_ns() - t0 - (uint64_t)pauses * req_ns;

    printf("cost: add %.1f ns; pause on top of the wait %.1f ns (incl. one add)\n",
           (double)add_ns / adds, (double)pause_ns / pauses);
    if ((double)add_ns / adds >= 200.0 || (double)pause_ns / pauses >= 200.0) {
        printf("  FAIL add and pause overhead must each be < 0.2 us\n");
        failures++;
    }
    rtd_destroy(ctx);
}

int main(int argc, char **argv)
{
    static const uint64_t delays[] = { 132, 982 };
    static const int cos[] = { 3, 16 };
    int iters = argc > 1 ? atoi(argv[1]) : 5000, i, c, d, idle_fd;

    if (iters <= 0) {
        fprintf(stderr, "usage: %s [iters]\n", argv[0]);
        return 2;
    }
    idle_fd = hold_cpu_idle();
    for (i = 0; i < MAX_COS; i++)
        sch.cos[i].stack = malloc(STACK_SIZE);

    run_costs();
    for (c = 0; c < 2; c++)
        for (d = 0; d < 2; d++) {
            run_mechanism(cos[c], delays[d], RTD_DEFAULT_SLACK_NS, iters);
            run_mechanism(cos[c], delays[d], 0, iters);
        }

    for (i = 0; i < MAX_COS; i++)
        free(sch.cos[i].stack);
    if (idle_fd >= 0)
        close(idle_fd);
    printf(failures ? "bench_rt_delay: %d check(s) FAILED\n" : "bench_rt_delay: all checks passed\n",
           failures);
    return failures ? 1 : 0;
}
