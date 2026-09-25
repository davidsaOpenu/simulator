#define _GNU_SOURCE
#include "core_affinity.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>

static int ca_single(int cpu_id, cpu_set_t *set)
{
    if (cpu_id < 0 || cpu_id >= CPU_SETSIZE)
        return -EINVAL;
    CPU_ZERO(set);
    CPU_SET(cpu_id, set);
    return 0;
}

int ca_pin_thread(int cpu_id)
{
    cpu_set_t set;
    int rc = ca_single(cpu_id, &set);

    if (rc)
        return rc;
    rc = pthread_setaffinity_np(pthread_self(), sizeof set, &set);   /* returns the errno value */
    return -rc;
}

int ca_pin_process(int cpu_id)
{
    cpu_set_t set;
    int rc = ca_single(cpu_id, &set);

    if (rc)
        return rc;
    return sched_setaffinity(0, sizeof set, &set) == 0 ? 0 : -errno;
}

int ca_verify_pinned(int cpu_id)
{
    cpu_set_t set;
    int rc;

    if (cpu_id < 0 || cpu_id >= CPU_SETSIZE)
        return -EINVAL;
    CPU_ZERO(&set);
    rc = pthread_getaffinity_np(pthread_self(), sizeof set, &set);
    if (rc)
        return -rc;
    return CPU_COUNT(&set) == 1 && CPU_ISSET(cpu_id, &set);
}
