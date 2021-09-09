#ifndef TEST_CONTEXT_H
#define TEST_CONTEXT_H

#include <stdint.h>
#include "logging_parser.h"

/* Public data you can pass when setting context */
typedef struct test_execution_context {
    const char* test_name;
    const char* test_case_name;
    const char* test_run_uuid;
    uint64_t    ssd_total_size_bytes;
    int64_t     test_start_timestamp_us;
} test_execution_context_t;

#ifdef __cplusplus
extern "C" {
#endif

void SSD_SET_TEST_CONTEXT(const test_execution_context_t* ctx);
void SSD_CLEAR_TEST_CONTEXT(void);

/* Build fully-populated LogMetadata */
LogMetadata LOG_META_MAKE(int64_t start_us, int64_t end_us);

/* Convenience macro to keep the callsites terse */
#define LOG_META(s,e) LOG_META_MAKE((s),(e))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TEST_CONTEXT_H */
