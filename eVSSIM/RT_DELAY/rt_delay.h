/*
 * rt_delay - per-slot delay accumulator and hybrid wait (sleep to target - slack, then spin).
 * Linux x86-64 only. rtd_add_* and rtd_pause are lock-free and may be called from any thread.
 */
#ifndef RT_DELAY_H
#define RT_DELAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sleep for about ns (relative); may return early or late, the spin phase finishes the wait. */
typedef void (*rtd_sleep_fn)(uint64_t ns, void *opaque);

typedef struct rtd_config {
    /* Spin window at the end of each wait. 0 = plain sleep (no spin). */
    uint64_t slack_ns;
    /* Sleep phase. NULL = absolute clock_nanosleep(CLOCK_MONOTONIC) on the calling thread. */
    rtd_sleep_fn sleep_fn;
    void *sleep_opaque;
    /* A pause is in band when its overshoot <= max(band_pct % of the request, band_min_ns). */
    uint32_t band_pct;
    uint64_t band_min_ns;
    /* A pause is late when its overshoot > late_ns. */
    uint64_t late_ns;
    /* Set the thread's timer slack to 1 ns on its first pause (the 50 us default exceeds slack_ns). */
    int set_timerslack;
} rtd_config;

#define RTD_DEFAULT_SLACK_NS    20000u
#define RTD_DEFAULT_BAND_PCT    5u
#define RTD_DEFAULT_BAND_MIN_NS 10000u
#define RTD_DEFAULT_LATE_NS     50000u

/* Per-slot counters. Overshoot = actual - requested wait. Empty pauses count only in pauses. */
typedef struct rtd_stats {
    uint64_t pauses;          /* rtd_pause calls */
    uint64_t waits;           /* of which found time to wait */
    uint64_t requested_ns;    /* total time taken from the slot and waited */
    uint64_t waited_ns;       /* total actual wait */
    double   mean_over_ns;
    uint64_t median_over_ns;  /* estimate, within ~7 % (log-linear histogram) */
    uint64_t p99_over_ns;     /* estimate, same resolution */
    uint64_t max_over_ns;
    uint64_t out_of_band;     /* waits whose overshoot exceeded the band */
    uint64_t late;            /* waits whose overshoot exceeded late_ns */
} rtd_stats;

typedef struct rtd_ctx rtd_ctx;

void rtd_config_default(rtd_config *cfg);

/* cfg NULL = defaults. Returns NULL on bad arguments or no memory. */
rtd_ctx *rtd_init(unsigned nslots, const rtd_config *cfg);
void rtd_destroy(rtd_ctx *ctx);
unsigned rtd_nslots(const rtd_ctx *ctx);

/* 0 on success, -1 on a bad slot. */
int rtd_add_ns(rtd_ctx *ctx, unsigned slot, uint64_t ns);
int rtd_add_us(rtd_ctx *ctx, unsigned slot, uint64_t us);

/* Wait out all time accumulated in the slot. Returns it in ns (0 if empty or a bad slot). */
uint64_t rtd_pause(rtd_ctx *ctx, unsigned slot);

uint64_t rtd_pending_ns(const rtd_ctx *ctx, unsigned slot);

/* 0 on success, -1 on a bad slot. */
int rtd_get_stats(const rtd_ctx *ctx, unsigned slot, rtd_stats *out);

/* One-line summary, snprintf-style return; -1 on a bad slot. */
int rtd_report(const rtd_ctx *ctx, unsigned slot, char *buf, size_t len);

/* CLOCK_MONOTONIC in ns. */
uint64_t rtd_now_ns(void);

#ifdef __cplusplus
}
#endif
#endif /* RT_DELAY_H */
