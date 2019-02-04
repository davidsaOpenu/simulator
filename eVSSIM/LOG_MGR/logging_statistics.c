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

#include "logging_statistics.h"


SSDStatistics stats_init(void) {
    SSDStatistics stats = {
            .write_count = 0,
            .write_speed = 0.0,
            .read_count = 0,
            .read_speed = 0.0,
            .garbage_collection_count = 0,
            .write_amplification = 0.0,
            .utilization = 0.0,
            .logical_write_count = 0,
            .write_wall_time = 0.0,
            .read_wall_time = 0,
            .current_wall_time = 0.0,
            .occupied_pages = 0
    };
    return stats;
}


int stats_json(SSDStatistics stats, Byte* buffer, int max_len) {
    return snprintf((char*) buffer, max_len, "{"
                    "\"write_count\":%d,"
                    "\"write_speed\":%f,"
                    "\"read_count\":%d,"
                    "\"read_speed\":%f,"
                    "\"garbage_collection_count\":%d,"
                    "\"write_amplification\":%f,"
                    "\"utilization\":%f"
                    "}",
                    stats.write_count, stats.write_speed, stats.read_count,
                    stats.read_speed, stats.garbage_collection_count,
                    stats.write_amplification, stats.utilization);
}


int stats_equal(SSDStatistics first, SSDStatistics second) {
    return first.write_count == second.write_count &&
           first.write_speed == second.write_speed &&
           first.read_count == second.read_count &&
           first.read_speed == second.read_speed &&
           first.garbage_collection_count == second.garbage_collection_count &&
           first.write_amplification == second.write_amplification &&
           first.utilization == second.utilization;
}
