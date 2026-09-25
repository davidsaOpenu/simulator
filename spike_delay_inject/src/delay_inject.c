#define _GNU_SOURCE
/* delay_inject.c - the hybrid wait (sleep to target - slack, then spin) and per-thread set-up.
 * Safe from any thread: the only shared state is the atomic slack; everything else is on the
 * caller's stack, and the libc calls used (clock_gettime, clock_nanosleep, prctl) are reentrant. */
#include <sys/prctl.h>
#include "delay_inject.h"
#include "core_affinity.h"

#include <errno.h>
#include <stdatomic.h>
#include <time.h>

#define DI_NS_PER_SEC 1000000000ull

static _Atomic uint64_t di_hybrid_slack = 20000;   /* 20 us */

uint64_t di_now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * DI_NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

void di_set_hybrid_slack_ns(uint64_t slack_ns)
{
    atomic_store(&di_hybrid_slack, slack_ns);
}

uint64_t di_get_hybrid_slack_ns(void)
{
    return atomic_load(&di_hybrid_slack);
}

/* Absolute sleep; restarting on EINTR cannot drift. */
static void di_sleep_until(uint64_t target_ns)
{
    struct timespec target = { (time_t)(target_ns / DI_NS_PER_SEC), (long)(target_ns % DI_NS_PER_SEC) };

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL) == EINTR)
        ;
}

uint64_t delay_inject_ns(uint64_t ns)
{
    const uint64_t t0 = di_now_ns(), target = t0 + ns, slack = atomic_load(&di_hybrid_slack);

    if (ns > slack)
        di_sleep_until(target - slack);
    while (di_now_ns() < target) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause" ::: "memory");   /* also stops -O2 hoisting the clock read */
#else
        __asm__ __volatile__("" ::: "memory");
#endif
    }
    return di_now_ns() - t0;
}

uint64_t delay_inject_us(uint64_t us)
{
    return delay_inject_ns(us * 1000ull);
}

int di_set_timerslack_ns(unsigned long ns)
{
    if (ns < DI_TIMERSLACK_MIN)   /* prctl reads <= 0 as "restore the 50 us default" */
        ns = DI_TIMERSLACK_MIN;
    return prctl(PR_SET_TIMERSLACK, ns, 0UL, 0UL, 0UL) == 0 ? 0 : -errno;
}

long di_get_timerslack_ns(void)
{
    int rc = prctl(PR_GET_TIMERSLACK, 0UL, 0UL, 0UL, 0UL);
    return rc < 0 ? -1L : (long)rc;
}

int delay_inject_thread_init(int core_id)
{
    int rc = di_set_timerslack_ns(DI_TIMERSLACK_MIN);

    if (rc == 0 && core_id >= 0)
        rc = delay_inject_pin_thread(core_id);
    return rc;
}

int delay_inject_pin_thread(int core_id)
{
    return ca_pin_thread(core_id);
}
