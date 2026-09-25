/* Core-pinning helpers shared by the interference workers and delay_inject. All return 0 or
 * -errno (ca_verify_pinned: 1 = pinned exactly to cpu_id, 0 = not). Thread-safe: stack only. */
#ifndef CORE_AFFINITY_H
#define CORE_AFFINITY_H
#ifdef __cplusplus
extern "C" {
#endif

int ca_pin_thread(int cpu_id);      /* the CALLING thread, via pthread_setaffinity_np */
int ca_pin_process(int cpu_id);     /* the calling process, via sched_setaffinity */
int ca_verify_pinned(int cpu_id);   /* is the caller's mask exactly {cpu_id}? */

#ifdef __cplusplus
}
#endif
#endif
