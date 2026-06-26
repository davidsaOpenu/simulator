#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "test_context.h"

/* Portable thread-local keyword */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #define TLS _Thread_local
#else
  #define TLS __thread
#endif

typedef struct ssd_tls_ctx {
    char*    test_name;
    char*    test_case_name;
    char*    test_run_uuid;
    char*    run_id;
    uint64_t ssd_total_size_bytes;
    int64_t  test_start_timestamp_us;
    int      initialized;
} ssd_tls_ctx_t;

static TLS ssd_tls_ctx_t g_tls_ctx = {0};

/* Process-global monotonic event seq; with run_id forms the idempotent ELK _id. */
static volatile uint64_t g_log_seq = 0;

/* Events emitted carrying a run_id = reconciliation manifest (exact ES doc count for the run). */
static volatile uint64_t g_run_event_count = 0;

static char* sdup_or_null(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* d = (char*)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static void tls_ctx_free_strings(ssd_tls_ctx_t* c) {
    if (!c) return;
    free(c->test_name);       c->test_name = NULL;
    free(c->test_case_name);  c->test_case_name = NULL;
    free(c->test_run_uuid);   c->test_run_uuid = NULL;
    free(c->run_id);          c->run_id = NULL;
}

void SSD_SET_TEST_CONTEXT(const test_execution_context_t* ctx) {
    tls_ctx_free_strings(&g_tls_ctx);
    memset(&g_tls_ctx, 0, sizeof(g_tls_ctx));
    if (!ctx) return;

    g_tls_ctx.test_name               = sdup_or_null(ctx->test_name);
    g_tls_ctx.test_case_name          = sdup_or_null(ctx->test_case_name);
    g_tls_ctx.test_run_uuid           = sdup_or_null(ctx->test_run_uuid);
    g_tls_ctx.run_id                  = sdup_or_null(ctx->run_id);
    g_tls_ctx.ssd_total_size_bytes    = ctx->ssd_total_size_bytes;
    g_tls_ctx.test_start_timestamp_us = ctx->test_start_timestamp_us;
    g_tls_ctx.initialized             = 1;
}

void SSD_CLEAR_TEST_CONTEXT(void) {
    tls_ctx_free_strings(&g_tls_ctx);
    memset(&g_tls_ctx, 0, sizeof(g_tls_ctx));
}

LogMetadata LOG_META_MAKE(uint8_t device_index, int64_t start_us, int64_t end_us) {
    LogMetadata m;
    memset(&m, 0, sizeof(m));

    m.logging_start_time = start_us;
    m.logging_end_time   = end_us;

    m.seq = __sync_fetch_and_add(&g_log_seq, 1);  /* every event, for the _id key */

    if (g_tls_ctx.initialized) {
        if (g_tls_ctx.test_name)
            snprintf(m.test_name,  sizeof(m.test_name),  "%s", g_tls_ctx.test_name);
        if (g_tls_ctx.test_case_name)
            snprintf(m.test_suite, sizeof(m.test_suite), "%s", g_tls_ctx.test_case_name);
        if (g_tls_ctx.test_run_uuid)
            snprintf(m.test_uuid,  sizeof(m.test_uuid),  "%s", g_tls_ctx.test_run_uuid);
        if (g_tls_ctx.run_id) {
            snprintf(m.run_id,     sizeof(m.run_id),     "%s", g_tls_ctx.run_id);
            __sync_fetch_and_add(&g_run_event_count, 1);
        }
        m.ssd_size_bytes = g_tls_ctx.ssd_total_size_bytes;
        if (m.ssd_size_bytes == 0) {
            m.ssd_size_bytes = (uint64_t)devices[device_index].pages_in_ssd * (uint64_t)GET_PAGE_SIZE(device_index);
        }
        m.test_start_time_us = g_tls_ctx.test_start_timestamp_us ? g_tls_ctx.test_start_timestamp_us: 0;
    } else {
        m.ssd_size_bytes = (uint64_t)devices[device_index].pages_in_ssd * (uint64_t)GET_PAGE_SIZE(device_index);
    }

    return m;
}

void SSD_SNAPSHOT_TEST_CONTEXT(test_context_snapshot_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!g_tls_ctx.initialized) return;

    if (g_tls_ctx.test_name)
        snprintf(out->test_name,  sizeof(out->test_name),  "%s", g_tls_ctx.test_name);
    if (g_tls_ctx.test_case_name)
        snprintf(out->test_suite, sizeof(out->test_suite), "%s", g_tls_ctx.test_case_name);
    if (g_tls_ctx.test_run_uuid)
        snprintf(out->test_uuid,  sizeof(out->test_uuid),  "%s", g_tls_ctx.test_run_uuid);
    if (g_tls_ctx.run_id)
        snprintf(out->run_id,     sizeof(out->run_id),     "%s", g_tls_ctx.run_id);
    out->ssd_total_size_bytes    = g_tls_ctx.ssd_total_size_bytes;
    out->test_start_timestamp_us = g_tls_ctx.test_start_timestamp_us;
    out->valid = 1;
}

void SSD_APPLY_TEST_CONTEXT(const test_context_snapshot_t* snap) {
    if (!snap || !snap->valid) return;

    test_execution_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.test_name               = snap->test_name[0]  ? snap->test_name  : NULL;
    ctx.test_case_name          = snap->test_suite[0] ? snap->test_suite : NULL;
    ctx.test_run_uuid           = snap->test_uuid[0]  ? snap->test_uuid  : NULL;
    ctx.run_id                  = snap->run_id[0]     ? snap->run_id     : NULL;
    ctx.ssd_total_size_bytes    = snap->ssd_total_size_bytes;
    ctx.test_start_timestamp_us = snap->test_start_timestamp_us;
    SSD_SET_TEST_CONTEXT(&ctx);
}

uint64_t SSD_GET_RUN_EVENT_COUNT(void) {
    return g_run_event_count;
}
