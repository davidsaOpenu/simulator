#ifndef TEST_CONTEXT_H
#define TEST_CONTEXT_H

#include <stdint.h>
#include "logging_parser.h"

/* Public data you can pass when setting context */
typedef struct test_execution_context {
    const char* test_name;
    const char* test_case_name;
    const char* test_run_uuid;
    const char* run_id;            /* per-invocation id (EVSSIM_RUN_ID); may be NULL */
    uint64_t    ssd_total_size_bytes;
    int64_t     test_start_timestamp_us;
} test_execution_context_t;

/* Self-contained context copy with owned storage, safe to hand to another thread
 * (the GC thread snapshots the test thread's context and re-applies it as its own TLS). */
typedef struct test_context_snapshot {
    int      valid;
    char     test_name[TEST_NAME_MAX];
    char     test_suite[TEST_SUITE_MAX];
    char     test_uuid[TEST_UUID_MAX];
    char     run_id[RUN_ID_MAX];
    uint64_t ssd_total_size_bytes;
    int64_t  test_start_timestamp_us;
} test_context_snapshot_t;

#ifdef __cplusplus
extern "C" {
#endif

void SSD_SET_TEST_CONTEXT(const test_execution_context_t* ctx);
void SSD_CLEAR_TEST_CONTEXT(void);

/* Copy the calling thread's current context into out (out->valid=0 if none). */
void SSD_SNAPSHOT_TEST_CONTEXT(test_context_snapshot_t* out);
/* Set the calling thread's context from a snapshot (no-op if snap is invalid). */
void SSD_APPLY_TEST_CONTEXT(const test_context_snapshot_t* snap);

/* Events emitted carrying a run_id = the reconciliation manifest. */
uint64_t SSD_GET_RUN_EVENT_COUNT(void);

/* Build fully-populated LogMetadata */
LogMetadata LOG_META_MAKE(uint8_t device_index, int64_t start_us, int64_t end_us);

/* Convenience macro to keep the callsites terse */
#define LOG_META(d,s,e) LOG_META_MAKE((d),(s),(e))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TEST_CONTEXT_H */
