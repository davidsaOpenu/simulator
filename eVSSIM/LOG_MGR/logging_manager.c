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

#include <unistd.h>
#include <stdlib.h>

#include "logging_manager.h"
#include "ssd_io_manager.h"
#include "ftl_perf_manager.h"
#include "vssim_config_manager.h"

/**
 * Transforms pages in a usec to megabytes in a second
 * @param {double} x pages in a usec
 */
#define PAGES_PER_USEC_TO_MEGABYTES_PER_SECOND(x) \
    ((((double) (x)) * (GET_PAGE_SIZE()) * (SECOND_IN_USEC)) / (MEGABYTE_IN_BYTES))


/**
 * The next slot number
 * @param i the current slot number
 */
#if(HANDLER_SIZE == 1)
#define NEXT_SLOT(i) 0
#else
#define NEXT_SLOT(i) (((i) + 1) % HANDLER_SIZE)
#endif

extern ssd_disk ssd;

/**
 * The hook to use when subscribing to an analyzer
 * @param stats the new stats from the analyzer
 * @param uid the unique id used when subscribing (pointer to the handler)
 */
static void on_analyzer_update(SSDStatistics stats, void* uid) {
    AnalyzerHandler* handler = (AnalyzerHandler*) uid;
    // first update the next statistics, and only then update the index
    // the only case which this does not solve is when this hook is called `HANDLER_SIZE` times
    // during the copy of the struct from here to the manager's main loop, which is extremely
    // unlikely
    handler->slots[NEXT_SLOT(handler->current_slot)] = stats;
    handler->current_slot = NEXT_SLOT(handler->current_slot);
}


LogManager* log_manager_init(void) {
    LogManager* manager = (LogManager*) malloc(sizeof(LogManager));
    if (manager == NULL)
        return NULL;
    manager->analyzers_count = 0;
    manager->subscribers_count = 0;
    manager->exit_loop_flag = 0;
    return manager;
}


int log_manager_subscribe(LogManager* manager, MonitorHook hook, void* uid) {
    if (manager->subscribers_count >= MAX_SUBSCRIBERS)
        return 1;
    manager->hooks_ids[manager->subscribers_count] = uid;
    manager->hooks[manager->subscribers_count++] = hook;
    return 0;
}


int log_manager_add_analyzer(LogManager* manager, RTLogAnalyzer* analyzer) {
    if (manager->analyzers_count == MAX_ANALYZERS)
        return 1;

    AnalyzerHandler* handler = &(manager->handlers[manager->analyzers_count++]);
    unsigned int slot_id;
    for (slot_id = 0; slot_id < HANDLER_SIZE; slot_id++)
        handler->slots[slot_id] = stats_init();
    handler->current_slot = 0;
    rt_log_analyzer_subscribe(analyzer, (MonitorHook) on_analyzer_update, (void*) handler);

    return 0;
}


void* log_manager_run(void* manager) {
    log_manager_loop((LogManager*) manager, -1);
    return NULL;
}

void log_manager_loop(LogManager* manager, int max_loops) {
    SSDStatistics old_stats = stats_init();
    int first_loop = 1;
    int loops = 0;
    while (max_loops < 0 || loops < max_loops) {
        // init the current statistics
        SSDStatistics stats = stats_init();
        ssd.current_stats = &stats;

        unsigned int analyzer_id;
        // update the statistics according to the different analyzers
        for (analyzer_id = 0; analyzer_id < manager->analyzers_count; analyzer_id++) {
            // copy the statistics to here, to avoid the chance of corruption
            // see the comment at `on_analyzer_update`
            AnalyzerHandler* handler = &(manager->handlers[analyzer_id]);
            SSDStatistics current_stats = handler->slots[handler->current_slot];

            stats.write_count += current_stats.write_count;
            stats.read_count += current_stats.read_count;
            stats.garbage_collection_count += current_stats.garbage_collection_count;
            stats.utilization += current_stats.utilization;

            stats.logical_write_count += current_stats.logical_write_count;
            stats.write_wall_time += current_stats.write_wall_time;
            stats.read_wall_time += current_stats.read_wall_time;
        }

        if (stats.logical_write_count == 0)
            stats.write_amplification = 0.0;
        else
            stats.write_amplification = ((double) stats.write_count) / stats.logical_write_count;

        if (stats.write_wall_time == 0)
            stats.write_speed = 0.0;
        else
            stats.write_speed = PAGES_PER_USEC_TO_MEGABYTES_PER_SECOND(
                ((double) stats.logical_write_count) / stats.write_wall_time
            );

        if (stats.read_wall_time == 0)
            stats.read_speed = 0.0;
        else
            stats.read_speed = PAGES_PER_USEC_TO_MEGABYTES_PER_SECOND(
                ((double) stats.read_count) / stats.read_wall_time
            );

<<<<<<< PATCH SET (64abbc WIP Big SSD)
        stats.read_wall_time = read_wall_time;
        stats.write_wall_time = write_wall_time;        

=======
        #ifdef MONITOR_DEBUG
>>>>>>> BASE      (de6106 Fix SSD stat correctness)
        validateSSDStat(&stats);
        #endif

        unsigned int subscriber_id;
        // call present hooks if the statistics changed
        if (first_loop || !stats_equal(old_stats, stats))
            for (subscriber_id = 0; subscriber_id < manager->subscribers_count; subscriber_id++){
                manager->hooks[subscriber_id](stats, manager->hooks_ids[subscriber_id]);
            }


        // update `loops` only if necessary (in order to avoid overflow)
        if (max_loops >= 0)
            loops++;

        first_loop = 0;
        old_stats = stats;

        // exit if needed
        if (manager->exit_loop_flag) {
            manager->exit_loop_flag = 0;
            break;
        }
        
        // wait before going to the next loop
        if (usleep(MANGER_LOOP_SLEEP))
            break;          // if an error occurred (probably a signal interrupt) just exit
    }
}


void log_manager_free(LogManager* manager) {
    free((void*) manager);
}
