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

#ifndef __LOGGING_STATISTICS_H__
#define __LOGGING_STATISTICS_H__


#include "logging_backend.h"

/**
 * The statistics gathered from the simulator
 */
typedef struct {
    /**
     * The number of physical page written
     */
    unsigned int write_count;
    /**
     * The writing speed in MB/s
     */
    double write_speed;
    /**
     * The number of physical page written
     */
    unsigned int read_count;
    /**
     * The reading speed in MB/s
     */
    double read_speed;
    /**
     * The number of garbage collection done
     */
    unsigned int garbage_collection_count;
    /**
     * The write amplification of the ssd
     */
    double write_amplification;
    /**
     * The utilization of the ssd
     */
    double utilization;
    /**
     * The number of logical writes done.
     */
    unsigned int logical_write_count;
    /**
     * Time in us spent on writes.
     */
    uint64_t write_wall_time;
    /**
     * Time in us spent on writes.
     */
    uint64_t read_wall_time;
} SSDStatistics;


/**
 * Return a new, empty statistics structure
 * @return a new, empty statistics structure
 */
SSDStatistics stats_init(void);


/**
 * Write a JSON representation of the statistics given to the buffer
 * @param stats the statistics to write
 * @param buffer the buffer to write the JSON to
 * @param max_len the maximum length to write
 * @return -1 if an error occurred, otherwise the number of characters written
 *         (including the terminating null byte)
 */
int stats_json(SSDStatistics stats, Byte* buffer, int max_len);


/**
 * Return whether the two statistics are equal
 * @param first the first statistics to compare
 * @param second the second statistics to compare
 * @return whether the two statistics are equal
 */
int stats_equal(SSDStatistics first, SSDStatistics second);


/**
 * print to stdout current statistics
*/
void printSSDStat(SSDStatistics *);

/**
 * Basic sanity checks for ssdStats, used for catching impossible stats
*/
void validateSSDStat(SSDStatistics *);


#endif
