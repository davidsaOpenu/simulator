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

#ifndef __ELK_EVENT_GENERATOR_H__
#define __ELK_EVENT_GENERATOR_H__

#include <stdint.h>

/**
 * A fixed-seed read/write/erase event generator, used to give the ELK metric gates a
 * ground truth they can be checked against.
 *
 * The plan is split into writers, one per (channel, die) pair of every disk. A writer's
 * stream depends only on the seed and on the writer's own identity, so a writer produces
 * the same ops whether it is walked on its own or as part of the whole plan.
 */

/**
 * The event types the generator produces
 */
typedef enum {
    ELK_GEN_READ = 0,
    ELK_GEN_PROGRAM,
    ELK_GEN_ERASE,
    /**
     * The number of event types
     * LEAVE AS LAST ITEM!
     */
    ELK_GEN_OP_COUNT
} elk_gen_op_type;

/**
 * A single planned event
 */
typedef struct {
    /**
     * The disk the event belongs to
     */
    uint8_t device_index;
    /**
     * The channel the event is issued on
     */
    uint32_t channel;
    /**
     * The die (flash) the event is issued on
     */
    uint32_t die;
    /**
     * The block the event touches
     */
    uint64_t block;
    /**
     * The page the event touches; always 0 for an erase, which is block wide
     */
    uint64_t page;
    /**
     * The type of the event
     */
    elk_gen_op_type type;
} elk_gen_op;

/**
 * The generation parameters; the whole plan is a pure function of these
 */
typedef struct {
    /**
     * The seed of the run
     */
    uint64_t seed;
    /**
     * The volume tier of the run, in bytes; the plan holds total_bytes / page_size events
     */
    uint64_t total_bytes;
    /**
     * The page size of the simulated disks, in bytes
     */
    uint32_t page_size;
    /**
     * The number of disks to generate for
     */
    uint8_t device_count;
    /**
     * The number of dies (flashes) per disk
     */
    uint32_t dies_per_device;
    /**
     * The number of channels per disk
     */
    uint32_t channels_per_device;
    /**
     * The number of blocks per die
     */
    uint64_t blocks_per_die;
    /**
     * The number of pages per block
     */
    uint64_t pages_per_block;
} elk_gen_config;

/**
 * The volume tier of a default run, in bytes
 */
#define ELK_GEN_DEFAULT_TOTAL_BYTES (16 * 1024 * 1024)

/**
 * The number of disks a tally can hold
 */
#define ELK_GEN_MAX_DEVICES 16

/**
 * The generator's own count of what it emitted: the ground truth of a run
 */
typedef struct {
    /**
     * The number of events emitted, per disk and per event type
     */
    uint64_t counts[ELK_GEN_MAX_DEVICES][ELK_GEN_OP_COUNT];
} elk_gen_tally;

/**
 * Fill in the parameters of a default run: the 16 MB tier over three disks
 * @param cfg the parameters to fill
 */
void elk_gen_default_config(elk_gen_config* cfg);

/**
 * Called once per planned event
 * @param op the planned event
 * @param ctx user defined data, passed through from the walk
 */
typedef void (*elk_gen_sink)(const elk_gen_op* op, void* ctx);

/**
 * The number of writers the plan is split into
 * @param cfg the generation parameters
 * @return one writer per (channel, die) pair of every disk
 */
uint64_t elk_gen_writer_count(const elk_gen_config* cfg);

/**
 * Walk the events of a single writer
 * @param cfg the generation parameters
 * @param writer the index of the writer, below elk_gen_writer_count()
 * @param sink the function to call for every event
 * @param ctx user defined data, passed to the sink
 */
void elk_gen_walk_writer(const elk_gen_config* cfg, uint64_t writer, elk_gen_sink sink, void* ctx);

/**
 * Walk the whole plan on the calling thread, one writer after the other
 * @param cfg the generation parameters
 * @param sink the function to call for every event
 * @param ctx user defined data, passed to the sink
 */
void elk_gen_walk(const elk_gen_config* cfg, elk_gen_sink sink, void* ctx);

/**
 * The name the log files use for an event type
 * @param type the event type
 * @return the log type name, as it appears in the "type" field of a log line
 */
const char* elk_gen_log_type_name(elk_gen_op_type type);

/**
 * Zero a tally
 * @param tally the tally to reset
 */
void elk_gen_tally_reset(elk_gen_tally* tally);

/**
 * Bring up the disks the plan needs: config, FTL, log manager, and the device lock
 * that keeps the background GC out of the run
 * @param cfg the generation parameters
 * @return 0 on success, nonzero if the config does not describe cfg->device_count disks
 */
int elk_gen_devices_up(const elk_gen_config* cfg);

/**
 * Take the disks down, draining the analyzers so every event reaches a log file
 * @param cfg the generation parameters
 */
void elk_gen_devices_down(const elk_gen_config* cfg);

/**
 * Emit an event through the ssd module of its disk, and count it
 * Has the shape of an elk_gen_sink, so it can be handed straight to a walk
 * @param op the event to emit
 * @param tally the elk_gen_tally to count the event in
 */
void elk_gen_emit(const elk_gen_op* op, void* tally);

/**
 * Write a tally next to the logs, for the gates to check the shipped events against
 * @param tally the tally of the run
 * @param cfg the generation parameters of the run
 * @param path the file to write
 * @return 0 on success, nonzero otherwise
 */
int elk_gen_tally_write(const elk_gen_tally* tally, const elk_gen_config* cfg, const char* path);

/**
 * Write an ssd.conf holding cfg's geometry, for INIT_SSD_CONFIG() to read
 * @param cfg the generation parameters
 * @param path the file to write
 * @return 0 on success, nonzero otherwise
 */
int elk_gen_write_ssd_conf(const elk_gen_config* cfg, const char* path);

#endif
