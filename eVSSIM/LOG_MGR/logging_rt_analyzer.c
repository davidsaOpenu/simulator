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


/**
 * Transforms pages in a usec to megabytes in a second
 * @param {double} x pages in a usec
 */
#define PAGES_IN_USEC_TO_MBS(x) \
    ((((double) (x)) * (GET_PAGE_SIZE()) * (SECOND_IN_USEC)) / (MEGABYTE_IN_BYTES))

RTLogStatistics *rt_log_stats;

RTLogAnalyzer* rt_log_analyzer_init(Logger_Pool* logger, unsigned int analyzer_id) {
    RTLogAnalyzer* analyzer = (RTLogAnalyzer*) malloc(sizeof(RTLogAnalyzer));
    if (analyzer == NULL)
        return NULL;
    analyzer->logger = logger;
    analyzer->subscribers_count = 0;
    analyzer->exit_loop_flag = 0;
    analyzer->reset_flag = 0;
    analyzer->rt_analyzer_id = analyzer_id;

    return analyzer;
}

void rt_log_stats_init(void) {
    rt_log_stats = (RTLogStatistics*) malloc(sizeof(RTLogStatistics) * FLASH_NB);

    uint32_t i;

    for (i = 0; i < FLASH_NB; i++) {
        rt_log_stats[i].current_wall_time = 0;
        rt_log_stats[i].logical_write_count = 0;
        rt_log_stats[i].occupied_pages = 0;
        rt_log_stats[i].read_wall_time = 0;
        rt_log_stats[i].write_wall_time = 0;
    }
}


int rt_log_analyzer_subscribe(RTLogAnalyzer* analyzer, MonitorHook hook, void* uid) {
    if (analyzer->subscribers_count >= MAX_SUBSCRIBERS)
        return 1;
    analyzer->hooks_ids[analyzer->subscribers_count] = uid;
    analyzer->hooks[analyzer->subscribers_count++] = hook;
    return 0;
}

void* rt_log_analyzer_run(void* analyzer) {
    rt_log_analyzer_loop((RTLogAnalyzer*) analyzer, -1);
    return NULL;
}

void rt_log_analyzer_loop(RTLogAnalyzer* analyzer, int max_logs) {
    // init the statistics
    SSDStatistics stats = stats_init();
    SSDStatistics old_stats = stats_init();

    // init additional variables
    unsigned int logical_write_count = 0;

    unsigned int subscriber_id;

    // run as long as necessary
    int first_loop = 1;
    int logs_read = 0;
    while (max_logs < 0 || logs_read < max_logs) {
        // read the log type, while listening to analyzer->exit_loop_flag
        int log_type;
        uint32_t bytes_read = 0;
        int bytes_read_last_read = 0;

        while (bytes_read < sizeof(log_type)) {
            if (analyzer->exit_loop_flag)
                break;
            if (analyzer->reset_flag) {
                // reset flag, reset stats and call subscribers
                // save utilization, as it needs to remain the same
                analyzer->reset_flag = 0;
                double util = stats.utilization;
                old_stats = stats_init();
                stats = stats_init();
                stats.utilization = old_stats.utilization = util;
                rt_log_stats[analyzer->rt_analyzer_id].write_wall_time = 0;
                rt_log_stats[analyzer->rt_analyzer_id].read_wall_time = 0;
                rt_log_stats[analyzer->rt_analyzer_id].logical_write_count = 0;
                logical_write_count = 0;
                for (subscriber_id = 0; subscriber_id < analyzer->subscribers_count; subscriber_id++)
                    analyzer->hooks[subscriber_id](stats, analyzer->hooks_ids[subscriber_id]);
            }
            bytes_read_last_read = logger_read(analyzer->logger, ((Byte*)&log_type) + bytes_read,
                                      sizeof(log_type) - bytes_read, RT_ANALYZER);
            if (0 == bytes_read_last_read) {
                // We have nothing to read, we will got into a penalty timeoff for the analyzer loop.
                // NOTE Ignoring interrupt-possible failures of the sleep.
                (void)usleep(100000); // 0.1 Seconds
            }
            bytes_read += bytes_read_last_read;
        }

        // exit if needed
        if (analyzer->exit_loop_flag) {
            analyzer->exit_loop_flag = 0;
            break;
        }

        // if the log type is invalid, cannot recover
        if (log_type < 0 || log_type >= LOG_UID_COUNT) {
            PERR("WARNING: unknown log type id! [%d]\n", log_type);
            RERR(, "WARNING: the log may be corrupted and unusable!\n");
        }

        // update the statistics according to the log
        switch (log_type) {
            case PHYSICAL_CELL_READ_LOG_UID:
            {
                PhysicalCellReadLog res;
                NEXT_PHYSICAL_CELL_READ_LOG(analyzer->logger, &res, RT_ANALYZER);
                stats.read_count++;
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time += CELL_READ_DELAY;
                rt_log_stats[analyzer->rt_analyzer_id].read_wall_time += rt_log_stats[analyzer->rt_analyzer_id].current_wall_time;
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time = 0;
                break;
            }
            case PHYSICAL_CELL_PROGRAM_LOG_UID:
            {
                PhysicalCellProgramLog res;
                NEXT_PHYSICAL_CELL_PROGRAM_LOG(analyzer->logger, &res, RT_ANALYZER);
                stats.write_count++;
                rt_log_stats[analyzer->rt_analyzer_id].occupied_pages++;
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time += CELL_PROGRAM_DELAY;
                rt_log_stats[analyzer->rt_analyzer_id].write_wall_time += rt_log_stats[analyzer->rt_analyzer_id].current_wall_time;
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time = 0;
                break;
            }
            case LOGICAL_CELL_PROGRAM_LOG_UID:
            {
                LogicalCellProgramLog res;
                NEXT_LOGICAL_CELL_PROGRAM_LOG(analyzer->logger, &res, RT_ANALYZER);
                rt_log_stats[analyzer->rt_analyzer_id].logical_write_count++;
                logical_write_count++;
                break;
            }
            case GARBAGE_COLLECTION_LOG_UID:
            {
                GarbageCollectionLog res;
                NEXT_GARBAGE_COLLECTION_LOG(analyzer->logger, &res, RT_ANALYZER);
                stats.garbage_collection_count++;
                break;
            }
            case REGISTER_READ_LOG_UID:
            {
                RegisterReadLog res;
                NEXT_REGISTER_READ_LOG(analyzer->logger, &res, RT_ANALYZER);
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time += REG_READ_DELAY;
                break;
            }
            case REGISTER_WRITE_LOG_UID:
            {
                RegisterWriteLog res;
                NEXT_REGISTER_WRITE_LOG(analyzer->logger, &res, RT_ANALYZER);
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time += REG_WRITE_DELAY;
                break;
            }
            case BLOCK_ERASE_LOG_UID:
            {
                BlockEraseLog res;
                NEXT_BLOCK_ERASE_LOG(analyzer->logger, &res, RT_ANALYZER);
                rt_log_stats[analyzer->rt_analyzer_id].occupied_pages -=
                    (rt_log_stats[analyzer->rt_analyzer_id].occupied_pages < PAGE_NB)?
                        rt_log_stats[analyzer->rt_analyzer_id].occupied_pages :
                        PAGE_NB;
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time += BLOCK_ERASE_DELAY;
                break;
            }
            case CHANNEL_SWITCH_TO_READ_LOG_UID:
            {
                ChannelSwitchToReadLog res;
                NEXT_CHANNEL_SWITCH_TO_READ_LOG(analyzer->logger, &res, RT_ANALYZER);
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time += CHANNEL_SWITCH_DELAY_R;
                break;
            }
            case CHANNEL_SWITCH_TO_WRITE_LOG_UID:
            {
                ChannelSwitchToWriteLog res;
                NEXT_CHANNEL_SWITCH_TO_WRITE_LOG(analyzer->logger, &res, RT_ANALYZER);
                rt_log_stats[analyzer->rt_analyzer_id].current_wall_time += CHANNEL_SWITCH_DELAY_W;
                break;
            }
            case OBJECT_ADD_PAGE_LOG_UID:
            {
                ObjectAddPageLog res;
                rt_log_stats[analyzer->rt_analyzer_id].occupied_pages++;
                NEXT_OBJECT_ADD_PAGE_LOG(analyzer->logger, &res, RT_ANALYZER);
                break;
            }
            case OBJECT_COPYBACK_LOG_UID:
            {
                ObjectCopyback res;
                rt_log_stats[analyzer->rt_analyzer_id].occupied_pages++;
                NEXT_OBJECT_COPYBACK_LOG(analyzer->logger, &res, RT_ANALYZER);
                break;
            }
            default:
                fprintf(stderr, "WARNING: unknown log type id! [%d]\n", log_type);
                fprintf(stderr, "WARNING: rt_log_analyzer_loop may not be up to date!\n");
                break;
        }
        
        if (rt_log_stats[analyzer->rt_analyzer_id].logical_write_count == 0)
            stats.write_amplification = 0.0;
        else
            stats.write_amplification = ((double) stats.write_count) / logical_write_count;

        if(logical_write_count > stats.write_count){
            printf("WARNNING: logged logical write before physical\n");
        }

        if (rt_log_stats[analyzer->rt_analyzer_id].read_wall_time == 0)
            stats.read_speed = 0.0;
        else
            stats.read_speed = PAGES_IN_USEC_TO_MBS(
                ((double) stats.read_count) / rt_log_stats[analyzer->rt_analyzer_id].read_wall_time
            );

        if (rt_log_stats[analyzer->rt_analyzer_id].write_wall_time == 0)
            stats.write_speed = 0.0;
        else
            stats.write_speed = PAGES_IN_USEC_TO_MBS(
                ((double) logical_write_count) / rt_log_stats[analyzer->rt_analyzer_id].write_wall_time
            );
        
        stats.utilization = ((double)rt_log_stats[analyzer->rt_analyzer_id].occupied_pages/ PAGES_IN_SSD);
        
        stats.read_wall_time = rt_log_stats[analyzer->rt_analyzer_id].read_wall_time;
        stats.write_wall_time = rt_log_stats[analyzer->rt_analyzer_id].write_wall_time;
        stats.logical_write_count = logical_write_count;
        

        // call present hooks if the statistics changed
        if (first_loop || !stats_equal(old_stats, stats))
            for (subscriber_id = 0; subscriber_id < analyzer->subscribers_count; subscriber_id++)
                analyzer->hooks[subscriber_id](stats, analyzer->hooks_ids[subscriber_id]);

        // update `logs_read` only if necessary (in order to avoid overflow)
        if (max_logs >= 0)
            logs_read++;

        first_loop = 0;
        old_stats = stats;
    }
}

void rt_log_analyzer_free(RTLogAnalyzer* analyzer, int free_logger) {
    if (free_logger) {
        logger_free(analyzer->logger);
    }
    free((void*) analyzer);
}

void rt_log_stats_free(void) {
    free(rt_log_stats);
}
