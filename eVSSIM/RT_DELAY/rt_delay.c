/*
 * __atomic builtins rather than <stdatomic.h>, which gcc 4.8 (QEMU's compiler) lacks. Relaxed
 * ordering is enough: the counters are independent and the accumulator only has to add up.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rt_delay.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/prctl.h>

#if !defined(__linux__) || !defined(__x86_64__)
#error "rt_delay supports Linux on x86-64 only"
#endif

#define RTD_NS_PER_SEC 1000000000ull
#define RTD_CACHE_LINE 64

#define RTD_LOAD(p)       __atomic_load_n((p), __ATOMIC_RELAXED)
#define RTD_ADD(p, v)     __atomic_fetch_add((p), (v), __ATOMIC_RELAXED)
#define RTD_XCHG(p, v)    __atomic_exchange_n((p), (v), __ATOMIC_RELAXED)
#define RTD_CAS(p, e, v)  __atomic_compare_exchange_n((p), (e), (v), 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)

/* Log-linear histogram: exact below 16 ns, then 8 buckets per power of two (within 1/16). */
#define RTD_HIST_EXACT   16u
#define RTD_HIST_SUB_LOG 3u
#define RTD_HIST_SUB     (1u << RTD_HIST_SUB_LOG)
#define RTD_HIST_BUCKETS (RTD_HIST_EXACT + (64u - 4u) * RTD_HIST_SUB)

struct rtd_slot {
    uint64_t acc_ns __attribute__((aligned(RTD_CACHE_LINE)));

    uint64_t pauses __attribute__((aligned(RTD_CACHE_LINE)));
    uint64_t waits;
    uint64_t requested_ns;
    uint64_t waited_ns;
    uint64_t over_sum_ns;
    uint64_t max_over_ns;
    uint64_t out_of_band;
    uint64_t late;
    uint64_t hist[RTD_HIST_BUCKETS];
};

struct rtd_ctx {
    rtd_config cfg;
    unsigned nslots;
    struct rtd_slot *slots;
};

static __thread int rtd_tls_timerslack_set;

uint64_t rtd_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * RTD_NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

void rtd_config_default(rtd_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->slack_ns = RTD_DEFAULT_SLACK_NS;
    cfg->sleep_fn = NULL;
    cfg->sleep_opaque = NULL;
    cfg->band_pct = RTD_DEFAULT_BAND_PCT;
    cfg->band_min_ns = RTD_DEFAULT_BAND_MIN_NS;
    cfg->late_ns = RTD_DEFAULT_LATE_NS;
    cfg->set_timerslack = 1;
}

rtd_ctx *rtd_init(unsigned nslots, const rtd_config *cfg)
{
    rtd_ctx *ctx;

    if (nslots == 0)
        return NULL;
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    if (cfg)
        ctx->cfg = *cfg;
    else
        rtd_config_default(&ctx->cfg);
    ctx->nslots = nslots;
    ctx->slots = aligned_alloc(RTD_CACHE_LINE, (size_t)nslots * sizeof(struct rtd_slot));
    if (!ctx->slots) {
        free(ctx);
        return NULL;
    }
    memset(ctx->slots, 0, (size_t)nslots * sizeof(struct rtd_slot));
    return ctx;
}

void rtd_destroy(rtd_ctx *ctx)
{
    if (!ctx)
        return;
    free(ctx->slots);
    free(ctx);
}

unsigned rtd_nslots(const rtd_ctx *ctx)
{
    return ctx->nslots;
}

int rtd_add_ns(rtd_ctx *ctx, unsigned slot, uint64_t ns)
{
    if (slot >= ctx->nslots)
        return -1;
    RTD_ADD(&ctx->slots[slot].acc_ns, ns);
    return 0;
}

int rtd_add_us(rtd_ctx *ctx, unsigned slot, uint64_t us)
{
    return rtd_add_ns(ctx, slot, us * 1000u);
}

uint64_t rtd_pending_ns(const rtd_ctx *ctx, unsigned slot)
{
    if (slot >= ctx->nslots)
        return 0;
    return RTD_LOAD(&ctx->slots[slot].acc_ns);
}

static unsigned rtd_hist_index(uint64_t v)
{
    unsigned e;

    if (v < RTD_HIST_EXACT)
        return (unsigned)v;
    e = 63u - (unsigned)__builtin_clzll(v);   /* e >= 4 */
    return RTD_HIST_EXACT + (e - 4u) * RTD_HIST_SUB +
           (unsigned)((v >> (e - RTD_HIST_SUB_LOG)) & (RTD_HIST_SUB - 1u));
}

static uint64_t rtd_hist_value(unsigned idx)
{
    unsigned e, sub;
    uint64_t width;

    if (idx < RTD_HIST_EXACT)
        return idx;
    e = (idx - RTD_HIST_EXACT) / RTD_HIST_SUB + 4u;
    sub = (idx - RTD_HIST_EXACT) % RTD_HIST_SUB;
    width = 1ull << (e - RTD_HIST_SUB_LOG);
    return (RTD_HIST_SUB + sub) * width + width / 2;
}

static void rtd_sleep_until(uint64_t target_ns)
{
    struct timespec target = { (time_t)(target_ns / RTD_NS_PER_SEC),
                               (long)(target_ns % RTD_NS_PER_SEC) };

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL) == EINTR)
        ;
}

static void rtd_set_timerslack(void)
{
    /* prctl reads 0 as "restore the default 50 us", so the minimum is 1 ns. */
    prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
    rtd_tls_timerslack_set = 1;
}

static void rtd_record(struct rtd_ctx *ctx, struct rtd_slot *s, uint64_t req, uint64_t waited,
                       uint64_t over)
{
    const rtd_config *cfg = &ctx->cfg;
    uint64_t band = req / 100u * cfg->band_pct + req % 100u * cfg->band_pct / 100u;
    uint64_t max = RTD_LOAD(&s->max_over_ns);

    if (band < cfg->band_min_ns)
        band = cfg->band_min_ns;
    RTD_ADD(&s->waits, 1);
    RTD_ADD(&s->requested_ns, req);
    RTD_ADD(&s->waited_ns, waited);
    RTD_ADD(&s->over_sum_ns, over);
    RTD_ADD(&s->hist[rtd_hist_index(over)], 1);
    if (over > band)
        RTD_ADD(&s->out_of_band, 1);
    if (over > cfg->late_ns)
        RTD_ADD(&s->late, 1);
    while (over > max && !RTD_CAS(&s->max_over_ns, &max, over))
        ;
}

uint64_t rtd_pause(rtd_ctx *ctx, unsigned slot)
{
    const rtd_config *cfg = &ctx->cfg;
    struct rtd_slot *s;
    uint64_t req, t0, target, now;

    if (slot >= ctx->nslots)
        return 0;
    s = &ctx->slots[slot];
    t0 = rtd_now_ns();
    req = RTD_XCHG(&s->acc_ns, 0);
    if (req) {
        if (cfg->set_timerslack && !rtd_tls_timerslack_set)
            rtd_set_timerslack();
        target = t0 + req;
        if (req > cfg->slack_ns) {
            if (cfg->sleep_fn) {
                now = rtd_now_ns();
                if (target - cfg->slack_ns > now)
                    cfg->sleep_fn(target - cfg->slack_ns - now, cfg->sleep_opaque);
            } else {
                rtd_sleep_until(target - cfg->slack_ns);
            }
        }
        while ((now = rtd_now_ns()) < target)
            __asm__ __volatile__("pause" ::: "memory");
        rtd_record(ctx, s, req, now - t0, now - target);
    }
    RTD_ADD(&s->pauses, 1);
    return req;
}

static uint64_t rtd_hist_rank(const uint64_t *hist, uint64_t rank)
{
    uint64_t seen = 0;
    unsigned i;

    for (i = 0; i < RTD_HIST_BUCKETS; i++) {
        seen += hist[i];
        if (seen >= rank)
            return rtd_hist_value(i);
    }
    return 0;
}

int rtd_get_stats(const rtd_ctx *ctx, unsigned slot, rtd_stats *out)
{
    const struct rtd_slot *s;
    uint64_t hist[RTD_HIST_BUCKETS], total = 0, over_sum;
    unsigned i;

    if (slot >= ctx->nslots)
        return -1;
    s = &ctx->slots[slot];
    memset(out, 0, sizeof(*out));
    out->pauses = RTD_LOAD(&s->pauses);
    out->waits = RTD_LOAD(&s->waits);
    out->requested_ns = RTD_LOAD(&s->requested_ns);
    out->waited_ns = RTD_LOAD(&s->waited_ns);
    over_sum = RTD_LOAD(&s->over_sum_ns);
    out->max_over_ns = RTD_LOAD(&s->max_over_ns);
    out->out_of_band = RTD_LOAD(&s->out_of_band);
    out->late = RTD_LOAD(&s->late);
    if (out->waits)
        out->mean_over_ns = (double)over_sum / (double)out->waits;
    for (i = 0; i < RTD_HIST_BUCKETS; i++) {
        hist[i] = RTD_LOAD(&s->hist[i]);
        total += hist[i];
    }
    if (total) {
        out->median_over_ns = rtd_hist_rank(hist, (total + 1) / 2);
        out->p99_over_ns = rtd_hist_rank(hist, total - total / 100);
    }
    return 0;
}

int rtd_report(const rtd_ctx *ctx, unsigned slot, char *buf, size_t len)
{
    rtd_stats st;
    double waits;

    if (rtd_get_stats(ctx, slot, &st) != 0)
        return -1;
    waits = st.waits ? (double)st.waits : 1.0;
    return snprintf(buf, len,
                    "slot %u: pauses %llu waits %llu requested %.6f s waited %.6f s | "
                    "overshoot us: mean %.2f median %.2f p99 %.2f max %.2f | "
                    "in band %.2f %% late(>%.0f us) %.3f %%",
                    slot, (unsigned long long)st.pauses, (unsigned long long)st.waits,
                    st.requested_ns / 1e9, st.waited_ns / 1e9,
                    st.mean_over_ns / 1e3, st.median_over_ns / 1e3, st.p99_over_ns / 1e3,
                    st.max_over_ns / 1e3,
                    100.0 * (double)(st.waits - st.out_of_band) / waits,
                    ctx->cfg.late_ns / 1e3, 100.0 * (double)st.late / waits);
}
