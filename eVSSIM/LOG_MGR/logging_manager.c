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

#include <stdlib.h>

#include "logging_manager.h"


/**
 * The next slot number
 * @param i the current slot number
 */
#define NEXT_SLOT(i) (((i) + 1) % HANDLER_SIZE)


/**
 * The hook to use when subscribing to an analyzer
 * @param stats the new stats from the analyzer
 * @param id the id used when subscribing (pointer to the handler)
 */
static void on_analyzer_update(SSDStatistics stats, void* id) {
    AnalyzerHandler* handler = (AnalyzerHandler*) id;
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
    return manager;
}


int log_manager_subscribe(LogManager* manager, MonitorHook hook, void* id) {
    if (manager->subscribers_count >= MAX_SUBSCRIBERS)
        return 1;
    manager->hooks_ids[manager->subscribers_count] = id;
    manager->hooks[manager->subscribers_count++] = hook;
    return 0;
}


int log_manager_add_analyzer(LogManager* manager, RTLogAnalyzer* analyzer) {
    if (manager->analyzers_count == MAX_ANALYZERS)
        return 1;

    AnalyzerHandler* handler = &(manager->handlers[manager->analyzers_count++]);
    unsigned int i;
    for (i = 0; i < HANDLER_SIZE; i++)
        handler->slots[i] = stats_init();
    handler->current_slot = 0;
    rt_log_analyzer_subscribe(analyzer, (MonitorHook) on_analyzer_update, (void*) handler);

    return 0;
}


void* log_manager_run(void* manager) {
    log_manager_loop((LogManager*) manager, -1);
    return NULL;
}


void log_manager_loop(LogManager* manager, int max_loops) {
    int loops = 0;
    while (max_loops < 0 || loops < max_loops) {
        // init the current statistics
        SSDStatistics stats = stats_init();

        // additional variables needed to calculate the statistics
        unsigned int logical_write_count = 0;
        double write_wall_time = 0;
        double read_wall_time = 0;

        unsigned int i;
        // update the statistics according to the different analyzers
        for (i = 0; i < manager->analyzers_count; i++) {
            // copy the statistics to here, to avoid the chance of corruption
            // see the comment at `on_analyzer_update`
            AnalyzerHandler* handler = &(manager->handlers[i]);
            SSDStatistics current_stats = handler->slots[handler->current_slot];

            stats.write_count += current_stats.write_count;
            stats.read_count += current_stats.read_count;
            stats.garbage_collection_count += current_stats.garbage_collection_count;
            stats.utilization += current_stats.utilization;

            logical_write_count += (unsigned int)
                    (current_stats.write_count / current_stats.write_amplification);
            write_wall_time += current_stats.write_count * current_stats.write_speed;
            read_wall_time += current_stats.read_count * current_stats.read_speed;
        }

        if (logical_write_count == 0)
            stats.write_amplification = 0.0;
        else
            stats.write_amplification = ((double) stats.write_count) / logical_write_count;

        if (stats.write_count == 0)
            stats.write_speed = 0.0;
        else
            stats.write_speed = write_wall_time / stats.write_count;

        if (stats.read_count == 0)
            stats.read_speed = 0.0;
        else
            stats.read_speed = read_wall_time / stats.read_count;

        // call present hooks
        for (i = 0; i < manager->subscribers_count; i++)
            manager->hooks[i](stats, manager->hooks_ids[i]);

        // update `loops` only if necessary (in order to avoid overflow)
        if (max_loops >= 0)
            loops++;
    }
}


void log_manager_free(LogManager* manager) {
    free((void*) manager);
}
