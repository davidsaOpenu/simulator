/*
 * Copyright 2017 The Open University of Israel
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

#include <stdio.h>
#include <stdlib.h>

#include "ftl_perf_manager.h"
#include "vssim_config_manager.h"

#include "logging_rt_analyzer.h"


SSDStatistics stats_init(void) {
    SSDStatistics stats = {
            .write_count = 0,
            .write_speed = 0.0,
            .read_count = 0,
            .read_speed = 0.0,
            .garbage_collection_count = 0,
            .write_amplification = 0.0,
            .utilization = 0.0
    };
    return stats;
}


int stats_json(SSDStatistics stats, Byte* buffer, int max_len) {
    return snprintf((char*) buffer, max_len, "{"
                    "\"write_count\"=%d,"
                    "\"write_speed\"=%f,"
                    "\"read_count\"=%d,"
                    "\"read_speed\"=%f,"
                    "\"garbage_collection_count\"=%d,"
                    "\"write_amplification\"=%f,"
                    "\"utilization\"=%f"
                    "}",
                    stats.write_count, stats.write_speed, stats.read_count,
                    stats.read_speed, stats.garbage_collection_count,
                    stats.write_amplification, stats.utilization);
}


/**
 * Transforms pages in a usec to megabytes in a second
 * @param {double} x pages in a usec
 */
#define PAGES_IN_USEC_TO_MBS(x) \
    (((double) (x) * GET_PAGE_SIZE() * SECOND_IN_USEC) / (MEGABYTE_IN_BYTES))


RTLogAnalyzer* rt_log_analyzer_init(Logger* logger) {
    RTLogAnalyzer* analyzer = (RTLogAnalyzer*) malloc(sizeof(RTLogAnalyzer));
    if (analyzer == NULL)
        return NULL;
    analyzer->logger = logger;
    analyzer->subscribers_count = 0;

    return analyzer;
}

int rt_log_analyzer_subscribe(RTLogAnalyzer* analyzer, MonitorHook hook, void* id) {
    if (analyzer->subscribers_count >= MAX_SUBSCRIBERS)
        return 1;
    analyzer->hooks_ids[analyzer->subscribers_count] = id;
    analyzer->hooks[analyzer->subscribers_count++] = hook;
    return 0;
}

void rt_log_analyzer_loop(RTLogAnalyzer* analyzer, int max_logs) {
    // init the statistics
    SSDStatistics stats = stats_init();

    // additional variables needed to calculate the statistics
    unsigned int logical_write_count = 0;
    int write_wall_time = 0;
    int read_wall_time = 0;
    int current_wall_time = 0;
    long occupied_pages = 0;

    // run as long as necessary
    int logs_read = 0;
    while (max_logs < 0 || logs_read < max_logs) {
        int log_type = next_log_type(analyzer->logger);
        // if the log type is invalid continue
        if (log_type < 0 || log_type >= LOG_UID_COUNT) {
            fprintf(stderr, "WARNING: unknown log type id! [%d]\n", log_type);
            fprintf(stderr, "WARNING: the log may be corrupted and unusable!\n");
            continue;
        }

        // update the statistics according to the log
        switch (log_type) {
            case PHYSICAL_CELL_READ_LOG_UID:
                NEXT_PHYSICAL_CELL_READ_LOG(analyzer->logger);
                stats.read_count++;
                current_wall_time += CELL_READ_DELAY;
                read_wall_time += current_wall_time;
                current_wall_time = 0;
                break;
            case PHYSICAL_CELL_PROGRAM_LOG_UID:
                NEXT_PHYSICAL_CELL_PROGRAM_LOG(analyzer->logger);
                stats.write_count++;
                occupied_pages++;
                current_wall_time += CELL_PROGRAM_DELAY;
                write_wall_time += current_wall_time;
                current_wall_time = 0;
                break;
            case LOGICAL_CELL_PROGRAM_LOG_UID:
                NEXT_LOGICAL_CELL_PROGRAM_LOG(analyzer->logger);
                logical_write_count++;
                break;
            case GARBAGE_COLLECTION_LOG_UID:
                NEXT_GARBAGE_COLLECTION_LOG(analyzer->logger);
                stats.garbage_collection_count++;
                break;
            case REGISTER_READ_LOG_UID:
                NEXT_REGISTER_READ_LOG(analyzer->logger);
                current_wall_time += REG_READ_DELAY;
                break;
            case REGISTER_WRITE_LOG_UID:
                NEXT_REGISTER_WRITE_LOG(analyzer->logger);
                current_wall_time += REG_WRITE_DELAY;
                break;
            case BLOCK_ERASE_LOG_UID:
                NEXT_BLOCK_ERASE_LOG(analyzer->logger);
                occupied_pages -= PAGE_NB;
                current_wall_time += BLOCK_ERASE_DELAY;
                break;
            case CHANNEL_SWITCH_TO_READ_LOG_UID:
                NEXT_CHANNEL_SWITCH_TO_READ_LOG(analyzer->logger);
                current_wall_time += CHANNEL_SWITCH_DELAY_R;
                break;
            case CHANNEL_SWITCH_TO_WRITE_LOG_UID:
                NEXT_CHANNEL_SWITCH_TO_WRITE_LOG(analyzer->logger);
                current_wall_time += CHANNEL_SWITCH_DELAY_W;
                break;
            default:
                fprintf(stderr, "WARNING: unknown log type id! [%d]\n", log_type);
                fprintf(stderr, "WARNING: rt_log_analyzer_loop may not be up to date!\n");
                continue;
        }

        if (logical_write_count == 0)
            stats.write_amplification = 0.0;
        else
            stats.write_amplification = ((double) stats.write_count) / logical_write_count;

        if (read_wall_time == 0)
            stats.read_speed = 0.0;
        else
            stats.read_speed = PAGES_IN_USEC_TO_MBS((double) stats.read_count / read_wall_time);

        if (write_wall_time == 0)
            stats.write_speed = 0.0;
        else
            stats.write_speed = PAGES_IN_USEC_TO_MBS(((double) stats.write_count) / write_wall_time);

        stats.utilization = ((double) occupied_pages) / PAGES_IN_SSD;


        // call present hooks
        unsigned int i;
        for (i = 0; i < analyzer->subscribers_count; i++)
            analyzer->hooks[i](stats, analyzer->hooks_ids[i]);

        // update `logs_read` only if necessary (in order to avoid overflow)
        if (max_logs >= 0)
            logs_read++;
    }
}

void rt_log_analyzer_free(RTLogAnalyzer* analyzer, int free_logger) {
    if (free_logger)
        logger_free(analyzer->logger);
    free((void*) analyzer);
}
