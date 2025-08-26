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
    uint64_t ssd_total_size_bytes;
    int64_t  test_start_timestamp_us;
    int      initialized;
} ssd_tls_ctx_t;

static TLS ssd_tls_ctx_t g_tls_ctx = {0};

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
}

void SSD_SET_TEST_CONTEXT(const test_execution_context_t* ctx) {
    tls_ctx_free_strings(&g_tls_ctx);
    memset(&g_tls_ctx, 0, sizeof(g_tls_ctx));
    if (!ctx) return;

    g_tls_ctx.test_name               = sdup_or_null(ctx->test_name);
    g_tls_ctx.test_case_name          = sdup_or_null(ctx->test_case_name);
    g_tls_ctx.test_run_uuid           = sdup_or_null(ctx->test_run_uuid);
    g_tls_ctx.ssd_total_size_bytes    = ctx->ssd_total_size_bytes;
    g_tls_ctx.test_start_timestamp_us = ctx->test_start_timestamp_us;
    g_tls_ctx.initialized             = 1;
}

void SSD_CLEAR_TEST_CONTEXT(void) {
    tls_ctx_free_strings(&g_tls_ctx);
    memset(&g_tls_ctx, 0, sizeof(g_tls_ctx));
}

LogMetadata LOG_META_MAKE(int64_t start_us, int64_t end_us) {
    LogMetadata m;
    memset(&m, 0, sizeof(m));

    m.logging_start_time = start_us;
    m.logging_end_time   = end_us;

    if (g_tls_ctx.initialized) {
        if (g_tls_ctx.test_name)
            snprintf(m.test_name,  sizeof(m.test_name),  "%s", g_tls_ctx.test_name);
        if (g_tls_ctx.test_case_name)
            snprintf(m.test_suite, sizeof(m.test_suite), "%s", g_tls_ctx.test_case_name);
        if (g_tls_ctx.test_run_uuid)
            snprintf(m.test_uuid,  sizeof(m.test_uuid),  "%s", g_tls_ctx.test_run_uuid);
        m.ssd_size_bytes = g_tls_ctx.ssd_total_size_bytes;
        if (m.ssd_size_bytes == 0) {
            m.ssd_size_bytes = (uint64_t)PAGES_IN_SSD * (uint64_t)GET_PAGE_SIZE();
        }
        m.test_start_time_us = g_tls_ctx.test_start_timestamp_us ? g_tls_ctx.test_start_timestamp_us: 0;
    } else {
        m.ssd_size_bytes = (uint64_t)PAGES_IN_SSD * (uint64_t)GET_PAGE_SIZE();
    }

    return m;
}
