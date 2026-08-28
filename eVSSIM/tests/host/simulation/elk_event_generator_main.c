/*
 * Copyright 2026 The Open University of Israel
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * The event generator tool: emits a fixed seed read / write / erase stream through the
 * real log manager and writes its own tally of what it emitted next to the logs. It
 * produces log files only, and never runs the FTL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "elk_event_generator.h"

#include "common.h"
#include "test_context.h"

#define DEFAULT_TALLY_PATH ELK_LOGGER_WRITER_LOGS_PATH "elk_generator_tally.json"
#define DEFAULT_TEST_NAME  "ElkEventGenerator"

static void usage(const char* program) {
    printf("usage: %s [--seed N] [--bytes N] [--test-name NAME] [--tally PATH]\n"
           "  --seed       the seed of the run (default %llu)\n"
           "  --bytes      the volume tier of the run (default %llu)\n"
           "  --test-name  the test.name the events carry (default %s)\n"
           "  --tally      where to write the tally (default %s)\n",
           program, (unsigned long long) 20587,
           (unsigned long long) ELK_GEN_DEFAULT_TOTAL_BYTES,
           DEFAULT_TEST_NAME, DEFAULT_TALLY_PATH);
}

int main(int argc, char** argv) {
    elk_gen_config cfg;
    elk_gen_tally tally;
    test_execution_context_t ctx;
    const char* tally_path = DEFAULT_TALLY_PATH;
    const char* test_name = DEFAULT_TEST_NAME;
    char run_uuid[TEST_UUID_MAX];
    int i;

    elk_gen_default_config(&cfg);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            cfg.seed = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--bytes") == 0 && i + 1 < argc)
            cfg.total_bytes = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--test-name") == 0 && i + 1 < argc)
            test_name = argv[++i];
        else if (strcmp(argv[i], "--tally") == 0 && i + 1 < argc)
            tally_path = argv[++i];
        else {
            usage(argv[0]);
            return strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }

    if (mkdir("data", 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "ERROR: cannot create the data directory: %s\n", strerror(errno));
        return 1;
    }
    if (elk_gen_write_ssd_conf(&cfg, "data/ssd.conf") != 0) {
        fprintf(stderr, "ERROR: cannot write data/ssd.conf: %s\n", strerror(errno));
        return 1;
    }

    /* the events carry the run's identity, so a gate can scope its queries to this run.
       the context is thread local: a parallel run has to set it on every writer thread */
    snprintf(run_uuid, sizeof(run_uuid), "%s-%llu-%d", test_name,
             (unsigned long long) cfg.seed, (int) getpid());
    memset(&ctx, 0, sizeof(ctx));
    ctx.test_name = test_name;
    ctx.test_case_name = DEFAULT_TEST_NAME;
    ctx.test_run_uuid = run_uuid;
    ctx.ssd_total_size_bytes = (uint64_t) cfg.pages_per_block * cfg.blocks_per_die
                               * cfg.dies_per_device * cfg.page_size;

    if (elk_gen_devices_up(&cfg) != 0) {
        fprintf(stderr, "ERROR: data/ssd.conf does not describe %u disks\n", cfg.device_count);
        return 1;
    }

    /* the simulated clock only starts once a disk is up: read before that, get_usec()
       returns 0 and the events lose test.start_time altogether */
    ctx.test_start_timestamp_us = get_usec();
    SSD_SET_TEST_CONTEXT(&ctx);

    elk_gen_tally_reset(&tally);
    /* one active writer: the whole plan is walked on this thread, writer by writer */
    elk_gen_walk(&cfg, elk_gen_emit, &tally);

    elk_gen_devices_down(&cfg);
    SSD_CLEAR_TEST_CONTEXT();

    if (elk_gen_tally_write(&tally, &cfg, tally_path) != 0) {
        fprintf(stderr, "ERROR: cannot write the tally to %s: %s\n", tally_path, strerror(errno));
        return 1;
    }

    printf("[elk_event_generator] run %s: %llu bytes, seed %llu, tally at %s\n",
           run_uuid, (unsigned long long) cfg.total_bytes,
           (unsigned long long) cfg.seed, tally_path);

    return 0;
}
