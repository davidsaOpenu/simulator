/* Minimal delay-injection helper: the hybrid wait decided by the spike (docs/RUNS_SUMMARY.md §5)
 * plus the per-thread set-up it needs. Thread-safe: no shared mutable state except one atomic. */
#ifndef DELAY_INJECT_H
#define DELAY_INJECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic clock, nanoseconds. */
uint64_t di_now_ns(void);

/* Hybrid wait: absolute clock_nanosleep() to target - slack, then busy-spin to target.
 * Returns the actual elapsed time in ns. A request <= slack is pure spin; slack 0 is a plain
 * sleep. Default slack 20 us: covers the ~10 us p99 of a sleep wake-up (5 us falls off a cliff). */
uint64_t delay_inject_ns(uint64_t ns);
uint64_t delay_inject_us(uint64_t us);
void     di_set_hybrid_slack_ns(uint64_t slack_ns);
uint64_t di_get_hybrid_slack_ns(void);

/* Per-thread set-up, once, before the first delay: minimum timer slack (Linux adds 50 us to every
 * non-RT sleep otherwise; prctl reads 0 as "reset to default", so the minimum is 1) and, when
 * core_id >= 0, pinning. QEMU's init_clocks() already does the slack part for its own threads. */
#define DI_TIMERSLACK_MIN 1UL
int  di_set_timerslack_ns(unsigned long ns);   /* 0 / -errno; clamps 0 up to 1 */
long di_get_timerslack_ns(void);               /* via PR_GET_TIMERSLACK, -1 on failure */
int  delay_inject_thread_init(int core_id);
int  delay_inject_pin_thread(int core_id);     /* wraps pthread_setaffinity_np */

#ifdef __cplusplus
}
#endif
#endif
