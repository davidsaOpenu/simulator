/* rt_delay unit tests. Timing checks only bound from below, so a loaded host cannot fail them. */
#define _GNU_SOURCE
#include "rt_delay.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;

#define CHECK(cond) do {                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "%s:%d: %s: CHECK(%s) failed\n", __FILE__, __LINE__,       \
                    __func__, #cond);                                                 \
            failures++;                                                               \
        }                                                                             \
    } while (0)

struct fake_sleep {
    int calls;
    uint64_t last_ns;
    uint64_t extra_ns;   /* sleep this much longer than asked */
};

static void fake_sleep_fn(uint64_t ns, void *opaque)
{
    struct fake_sleep *fs = opaque;
    uint64_t total = ns + fs->extra_ns;
    struct timespec ts = { (time_t)(total / 1000000000ull), (long)(total % 1000000000ull) };

    fs->calls++;
    fs->last_ns = ns;
    nanosleep(&ts, NULL);
}

/* The sleep phase stops slack_ns short of the target; a request inside the window only spins. */
static void test_hybrid_split(void)
{
    struct fake_sleep fs = { 0, 0, 0 };
    rtd_config cfg;
    rtd_ctx *ctx;

    rtd_config_default(&cfg);
    cfg.sleep_fn = fake_sleep_fn;
    cfg.sleep_opaque = &fs;
    ctx = rtd_init(1, &cfg);

    rtd_add_us(ctx, 0, 500);
    rtd_pause(ctx, 0);
    CHECK(fs.calls == 1);
    CHECK(fs.last_ns <= 500000 - RTD_DEFAULT_SLACK_NS);

    rtd_add_us(ctx, 0, 15);
    rtd_pause(ctx, 0);
    CHECK(fs.calls == 1);
    rtd_destroy(ctx);
}

/* The spin phase finishes a wait whose sleep phase woke early. */
static void early_sleep_fn(uint64_t ns, void *opaque)
{
    (void)ns;
    (void)opaque;
}

static void test_never_returns_early(void)
{
    rtd_config cfg;
    rtd_ctx *ctx;
    uint64_t t0;

    rtd_config_default(&cfg);
    cfg.sleep_fn = early_sleep_fn;
    ctx = rtd_init(1, &cfg);
    rtd_add_us(ctx, 0, 300);
    t0 = rtd_now_ns();
    CHECK(rtd_pause(ctx, 0) == 300000);
    CHECK(rtd_now_ns() - t0 >= 300000);
    rtd_destroy(ctx);
}

/* 1 ms request, band 50 us, late 50 us; oversleep by 120 us: out of band and late. */
static void test_overshoot_stats(void)
{
    struct fake_sleep fs = { 0, 0, 120000 };
    rtd_config cfg;
    rtd_ctx *ctx;
    rtd_stats st;

    rtd_config_default(&cfg);
    cfg.slack_ns = 0;
    cfg.sleep_fn = fake_sleep_fn;
    cfg.sleep_opaque = &fs;
    ctx = rtd_init(1, &cfg);

    rtd_add_us(ctx, 0, 1000);
    rtd_pause(ctx, 0);
    rtd_get_stats(ctx, 0, &st);
    CHECK(st.waits == 1);
    CHECK(st.out_of_band == 1);
    CHECK(st.late == 1);
    CHECK(st.max_over_ns >= 120000);
    CHECK(st.waited_ns - st.requested_ns == st.max_over_ns);
    /* One sample: the histogram estimate is its bucket midpoint, within 1/16 of it. */
    CHECK(st.median_over_ns == st.p99_over_ns);
    CHECK(st.median_over_ns + st.max_over_ns / 16 >= st.max_over_ns);
    CHECK(st.median_over_ns <= st.max_over_ns + st.max_over_ns / 16);
    rtd_destroy(ctx);
}

/* Many threads add and pause on shared slots: every added ns is taken and waited exactly once. */
#define RACE_THREADS 8
#define RACE_SLOTS   5
#define RACE_ITERS   4000

#define ADD(p, v) __atomic_fetch_add((p), (v), __ATOMIC_RELAXED)
#define LOAD(p)   __atomic_load_n((p), __ATOMIC_RELAXED)

struct race_shared {
    rtd_ctx *ctx;
    uint64_t added[RACE_SLOTS];
    uint64_t taken[RACE_SLOTS];
    uint64_t pauses[RACE_SLOTS];
    int go;
};

struct race_arg {
    struct race_shared *sh;
    unsigned seed;
};

static void *race_thread(void *p)
{
    struct race_arg *a = p;
    struct race_shared *sh = a->sh;
    unsigned seed = a->seed;
    int i;

    while (!__atomic_load_n(&sh->go, __ATOMIC_ACQUIRE))
        ;
    for (i = 0; i < RACE_ITERS; i++) {
        unsigned slot = (unsigned)rand_r(&seed) % RACE_SLOTS;
        uint64_t ns = 1 + (uint64_t)(rand_r(&seed) % 3000);   /* up to 3 us */

        if (rand_r(&seed) % 3) {
            rtd_add_ns(sh->ctx, slot, ns);
            ADD(&sh->added[slot], ns);
        } else {
            ADD(&sh->taken[slot], rtd_pause(sh->ctx, slot));
            ADD(&sh->pauses[slot], 1);
        }
    }
    return NULL;
}

static void test_concurrent_add_and_pause(void)
{
    static struct race_shared sh;
    struct race_arg args[RACE_THREADS];
    pthread_t th[RACE_THREADS];
    unsigned i;

    memset(&sh, 0, sizeof(sh));
    sh.ctx = rtd_init(RACE_SLOTS, NULL);
    for (i = 0; i < RACE_THREADS; i++) {
        args[i].sh = &sh;
        args[i].seed = 12345u + i;
        pthread_create(&th[i], NULL, race_thread, &args[i]);
    }
    __atomic_store_n(&sh.go, 1, __ATOMIC_RELEASE);
    for (i = 0; i < RACE_THREADS; i++)
        pthread_join(th[i], NULL);

    for (i = 0; i < RACE_SLOTS; i++) {
        rtd_stats st;

        /* Drain what is left. */
        ADD(&sh.taken[i], rtd_pause(sh.ctx, i));
        ADD(&sh.pauses[i], 1);
        rtd_get_stats(sh.ctx, i, &st);
        CHECK(LOAD(&sh.added[i]) == LOAD(&sh.taken[i]));
        CHECK(st.requested_ns == LOAD(&sh.added[i]));
        CHECK(st.pauses == LOAD(&sh.pauses[i]));
        CHECK(st.waited_ns >= st.requested_ns);
    }
    rtd_destroy(sh.ctx);
}

int main(void)
{
    test_hybrid_split();
    test_never_returns_early();
    test_overshoot_stats();
    test_concurrent_add_and_pause();

    if (failures) {
        fprintf(stderr, "test_rt_delay: %d check(s) failed\n", failures);
        return 1;
    }
    printf("test_rt_delay: all tests passed\n");
    return 0;
}
