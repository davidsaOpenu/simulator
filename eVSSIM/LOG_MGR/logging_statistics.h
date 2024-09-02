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
    uint64_t write_count;
    /**
     * The writing speed in MB/s =  logical_write_count / write_wall_time 
     */
    double write_speed;
    /**
     * The number of physical page written
     */
    uint64_t read_count;
    /**
     * The reading speed in MB/s
     */
    double read_speed;
    /**
     * The number of garbage collection done
     */
    uint64_t garbage_collection_count;
    /**
     * The write amplification of the ssd
     */
    double write_amplification;
    /**
     * The number of occupied_pages in the ssd, signed because it could, temporarily drop below 0.
     */
    __int128 occupied_pages;
    /**
     * The utilization of the ssd
     */
    double utilization;
<<<<<<< PATCH SET (10c5d9 Support for Big SSD)
    /**
     * The number of logical writes done.
     */
    uint64_t logical_write_count;
    /**
     * Time in us spent on writes.TODO:rename
     */
    uint64_t write_wall_time;
    /**
     * Time in us spent on writes.
     */
    uint64_t read_wall_time;
    /**
     * log_id used for log server sync.
     */
    uint64_t log_id;
    /**
     * The number of physical page erase actions
     */
    uint64_t block_erase_count;
    /**
     * The number of chennel switches to write
     */
    uint64_t channel_switch_to_write;
    /**
     * The number of chennel switches to write
     */
    uint64_t channel_switch_to_read;

=======
>>>>>>> BASE      (90e637 handle-depend-on-instructions.sh recives base64-encoded comm)
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


#endif
