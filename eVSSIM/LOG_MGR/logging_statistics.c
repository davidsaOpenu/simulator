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
            .occupied_pages = 0,
            .channel_switch_to_read = 0,
            .channel_switch_to_write = 0,
            .block_erase_count = 0,
            .log_id = 0
    };
    return stats;
}


int stats_json(SSDStatistics stats, Byte* buffer, int max_len) {
    return snprintf((char*) buffer, max_len, "{"
                    "\"write_count\":%lu,"
                    "\"write_speed\":%f,"
                    "\"read_count\":%lu,"
                    "\"read_speed\":%f,"
                    "\"garbage_collection_count\":%lu,"
                    "\"write_amplification\":%f,"
                    "\"utilization\":%f,"
                    "\"logical_write_count\":%lu,"
                    "\"write_wall_time\":%lu,"
                    "\"read_wall_time\":%lu,"
                    "\"channel_switch_to_read\":%lu,"
                    "\"channel_switch_to_write\":%lu,"
                    "\"block_erase_count\":%lu"
                    "}",
                    stats.write_count, stats.write_speed, stats.read_count,
                    stats.read_speed, stats.garbage_collection_count,
                    stats.write_amplification, stats.utilization,
                    stats.logical_write_count, stats.write_wall_time, stats.read_wall_time,
                    stats.channel_switch_to_read, stats.channel_switch_to_write, stats.block_erase_count);
}


int stats_equal(SSDStatistics first, SSDStatistics second) {
    return first.write_count == second.write_count &&
           first.write_speed == second.write_speed &&
           first.read_count == second.read_count &&
           first.read_speed == second.read_speed &&
           first.garbage_collection_count == second.garbage_collection_count &&
           first.write_amplification == second.write_amplification &&
           first.occupied_pages == second.occupied_pages &&
           first.utilization == second.utilization &&
           first.logical_write_count == second.logical_write_count &&
           first.write_wall_time == second.write_wall_time &&
           first.read_wall_time == second.read_wall_time &&
           first.block_erase_count == second.block_erase_count &&
           first.channel_switch_to_write == second.channel_switch_to_write &&
           first.channel_switch_to_read == second.channel_switch_to_read &&
           first.log_id == second.log_id;
}

void printSSDStat(SSDStatistics *stat){
    fprintf(stdout, "SSDStat:\n");
    fprintf(stdout, "\twrite_count = %lu\n", stat->write_count);
    fprintf(stdout, "\twrite_speed = %f\n", stat->write_speed);
    fprintf(stdout, "\tread_count = %lu\n", stat->read_count);
    fprintf(stdout, "\tread_speed = %f\n", stat->read_speed);
    fprintf(stdout, "\tgarbage_collection_count = %lu\n", stat->garbage_collection_count);
    fprintf(stdout, "\twrite_amplification = %f\n", stat->write_amplification);
    fprintf(stdout, "\tutilization = %f\n", stat->utilization);  
    fprintf(stdout, "\tlogical_write_count = %lu\n", stat->logical_write_count);  
    fprintf(stdout, "\twrite_wall_time = %lu\n", stat->write_wall_time);  
    fprintf(stdout, "\tread_wall_time = %lu\n", stat->read_wall_time);  
    fprintf(stdout, "\tblock_erase_count = %lu\n", stat->block_erase_count);  
    fprintf(stdout, "\tchannel_switch_to_write = %lu\n", stat->channel_switch_to_write);  
    fprintf(stdout, "\tchannel_switch_to_read = %lu\n", stat->channel_switch_to_read);  
};

void validateSSDStat(SSDStatistics *stat){
    if(stat->utilization < 0){
        fprintf(stderr, "bad utilization : %ff", stat->utilization);
    }
    
    //we don't cache writes so write amp cant be less then 1
    //2.2 was chosen as a 'good' upper limit for write amp. rben: updated to 10, there are test with lots of garbage collections, causing bad write amp, just an indication of bad ftl algorithem?
    if((stat->write_amplification < 0.9999f && stat->write_amplification != 0) || stat->write_amplification > 10){
        if(stat->logical_write_count - 1 < stat->write_count){
            fprintf(stderr, "bad write_amplification : %ff but within margin\n", stat->write_amplification);
        }
        fprintf(stderr, "bad write_amplification : %ff\n", stat->write_amplification);
    }

    if(stat->write_speed < 0){
        fprintf(stderr, "bad write_speed : %ff\n", stat->write_speed);
    }
    if(stat->read_speed < 0){
        fprintf(stderr, "bad write_speed : %ff\n", stat->write_speed);
    }
}
