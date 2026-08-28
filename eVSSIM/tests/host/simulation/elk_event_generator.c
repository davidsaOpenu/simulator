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

#include "elk_event_generator.h"

#include <stdio.h>
#include <string.h>

#include "common.h"

/**
 * The share of the events, out of ELK_GEN_MIX_SCALE, given to each type
 */
#define ELK_GEN_MIX_SCALE   10
#define ELK_GEN_MIX_PROGRAM 5
#define ELK_GEN_MIX_READ    4

/**
 * splitmix64: a fixed, self contained generator, so a stream does not depend on the
 * libc the tests happen to be built against
 * @param state the state to advance
 * @return the next value of the sequence
 */
static uint64_t elk_gen_random(uint64_t* state) {
    uint64_t z;

    *state += 0x9E3779B97F4A7C15ULL;
    z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;

    return z ^ (z >> 31);
}

/**
 * The seed of a single writer; depends on the writer's identity only, so the writer
 * produces the same stream no matter how many writers run beside it
 * @param seed the seed of the run
 * @param device_index the disk of the writer
 * @param die the die of the writer
 * @return the writer's seed
 */
static uint64_t elk_gen_writer_seed(uint64_t seed, uint8_t device_index, uint32_t die) {
    return seed * 0x9E3779B97F4A7C15ULL + ((uint64_t) device_index << 32) + die + 1;
}

/**
 * The number of events the plan holds
 * @param cfg the generation parameters
 * @return the number of events, derived from the volume tier
 */
static uint64_t elk_gen_ops(const elk_gen_config* cfg) {
    if (cfg->page_size == 0)
        return 0;
    return cfg->total_bytes / cfg->page_size;
}

/**
 * The type of an event, drawn from the fixed read / program / erase mix
 * @param draw a value of the writer's sequence
 * @return the type of the event
 */
static elk_gen_op_type elk_gen_type(uint64_t draw) {
    uint64_t bucket = draw % ELK_GEN_MIX_SCALE;

    if (bucket < ELK_GEN_MIX_PROGRAM)
        return ELK_GEN_PROGRAM;
    if (bucket < ELK_GEN_MIX_PROGRAM + ELK_GEN_MIX_READ)
        return ELK_GEN_READ;

    return ELK_GEN_ERASE;
}

void elk_gen_default_config(elk_gen_config* cfg) {
    memset(cfg, 0, sizeof(*cfg));

    cfg->seed = 20587;
    cfg->total_bytes = ELK_GEN_DEFAULT_TOTAL_BYTES;
    cfg->page_size = 4096;
    cfg->device_count = 3;
    /* two dies per channel, so that a (channel, die) pair is not just a channel */
    cfg->dies_per_device = 8;
    cfg->channels_per_device = 4;
    cfg->blocks_per_die = 64;
    cfg->pages_per_block = 8;
}

uint64_t elk_gen_writer_count(const elk_gen_config* cfg) {
    return (uint64_t) cfg->device_count * cfg->dies_per_device;
}

void elk_gen_walk_writer(const elk_gen_config* cfg, uint64_t writer, elk_gen_sink sink, void* ctx) {
    uint64_t writers = elk_gen_writer_count(cfg);
    uint64_t ops = elk_gen_ops(cfg);
    uint8_t device_index;
    uint32_t die;
    uint64_t state;
    uint64_t writer_ops;
    uint64_t i;

    if (writer >= writers || cfg->blocks_per_die == 0 || cfg->pages_per_block == 0)
        return;

    device_index = (uint8_t) (writer / cfg->dies_per_device);
    die = (uint32_t) (writer % cfg->dies_per_device);
    state = elk_gen_writer_seed(cfg->seed, device_index, die);
    /* the leftover events go to the lowest numbered writers, one each */
    writer_ops = ops / writers + (writer < ops % writers ? 1 : 0);

    for (i = 0; i < writer_ops; i++) {
        uint64_t draw = elk_gen_random(&state);
        elk_gen_op op;

        op.device_index = device_index;
        op.die = die;
        /* the same die to channel mapping the io manager uses */
        op.channel = cfg->channels_per_device ? die % cfg->channels_per_device : 0;
        op.type = elk_gen_type(draw);
        op.block = (draw >> 8) % cfg->blocks_per_die;
        op.page = op.type == ELK_GEN_ERASE ? 0 : (draw >> 24) % cfg->pages_per_block;

        sink(&op, ctx);
    }
}

void elk_gen_walk(const elk_gen_config* cfg, elk_gen_sink sink, void* ctx) {
    uint64_t writers = elk_gen_writer_count(cfg);
    uint64_t writer;

    for (writer = 0; writer < writers; writer++)
        elk_gen_walk_writer(cfg, writer, sink, ctx);
}

const char* elk_gen_log_type_name(elk_gen_op_type type) {
    switch (type) {
        case ELK_GEN_READ:    return "PhysicalCellReadLog";
        case ELK_GEN_PROGRAM: return "PhysicalCellProgramLog";
        case ELK_GEN_ERASE:   return "BlockEraseLog";
        default:              return "UnknownLog";
    }
}

void elk_gen_tally_reset(elk_gen_tally* tally) {
    memset(tally, 0, sizeof(*tally));
}

/**
 * Count one emitted event
 * @param tally the tally of the run, shared by all the writers
 * @param op the event that was emitted
 */
static void elk_gen_tally_add(elk_gen_tally* tally, const elk_gen_op* op) {
    if (tally == NULL || op->device_index >= ELK_GEN_MAX_DEVICES)
        return;
    __atomic_fetch_add(&tally->counts[op->device_index][op->type], 1, __ATOMIC_RELAXED);
}

void elk_gen_emit(const elk_gen_op* op, void* tally) {
    unsigned int block = (unsigned int) op->block;
    unsigned int page = (unsigned int) op->page;

    /* the events are emitted by the ssd module itself, from its public entry points:
       the generator plays the part of the firmware driving the dies, and never writes
       a log record of its own. the channel of the event is the one the io manager
       derives from the die, which is the mapping the plan already follows */
    switch (op->type) {
        case ELK_GEN_READ:
            SSD_PAGE_READ(op->device_index, op->die, block, page, 0, READ);
            elk_gen_tally_add((elk_gen_tally*) tally, op);
            break;
        case ELK_GEN_PROGRAM:
            SSD_PAGE_WRITE(op->device_index, op->die, block, page, 0, WRITE);
            elk_gen_tally_add((elk_gen_tally*) tally, op);
            break;
        case ELK_GEN_ERASE:
            SSD_BLOCK_ERASE(op->device_index, op->die, block, ERASE);
            elk_gen_tally_add((elk_gen_tally*) tally, op);
            break;
        default:
            break;
    }
}

int elk_gen_devices_up(const elk_gen_config* cfg) {
    uint8_t device;

    INIT_SSD_CONFIG();
    if (device_count != cfg->device_count)
        return -1;

    for (device = 0; device < device_count; device++) {
        FTL_INIT(device);
        INIT_LOG_MANAGER(device);
        /* the plan owns every erase, so the background GC thread must not add any of
           its own: holding the device lock keeps it out, as the host tests do */
        LOCK_DEVICE(device);
    }

    return 0;
}

void elk_gen_devices_down(const elk_gen_config* cfg) {
    uint8_t device;

    for (device = 0; device < cfg->device_count; device++) {
        UNLOCK_DEVICE(device);
        FTL_TERM(device);
        /* termination drains the analyzers, so every event is in a log file from here on */
        TERM_LOG_MANAGER(device);
    }
    TERM_SSD_CONFIG();
}

int elk_gen_tally_write(const elk_gen_tally* tally, const elk_gen_config* cfg, const char* path) {
    FILE* file = fopen(path, "w");
    uint8_t device;
    int type;

    if (file == NULL)
        return -1;

    fprintf(file, "{\n");
    fprintf(file, "  \"seed\": %llu,\n", (unsigned long long) cfg->seed);
    fprintf(file, "  \"total_bytes\": %llu,\n", (unsigned long long) cfg->total_bytes);
    fprintf(file, "  \"page_size\": %u,\n", cfg->page_size);
    fprintf(file, "  \"writers\": %llu,\n", (unsigned long long) elk_gen_writer_count(cfg));
    fprintf(file, "  \"devices\": [\n");
    for (device = 0; device < cfg->device_count && device < ELK_GEN_MAX_DEVICES; device++) {
        fprintf(file, "    { \"device_index\": %u", device);
        for (type = 0; type < ELK_GEN_OP_COUNT; type++)
            fprintf(file, ", \"%s\": %llu", elk_gen_log_type_name((elk_gen_op_type) type),
                    (unsigned long long) tally->counts[device][type]);
        fprintf(file, " }%s\n", device + 1 < cfg->device_count ? "," : "");
    }
    fprintf(file, "  ]\n}\n");

    return fclose(file);
}

int elk_gen_write_ssd_conf(const elk_gen_config* cfg, const char* path) {
    FILE* file = fopen(path, "w");
    uint8_t device;

    if (file == NULL)
        return -1;

    for (device = 0; device < cfg->device_count; device++) {
        fprintf(file,
                "[nvme%02u]\n"
                "FILE_NAME ./data/ssd%u.img\n"
                "PAGE_SIZE %u\n"
                "PAGE_NB %llu\n"
                "SECTOR_SIZE 1\n"
                "FLASH_NB %u\n"
                "BLOCK_NB %llu\n"
                "PLANES_PER_FLASH 1\n"
                "REG_WRITE_DELAY 82\n"
                "CELL_PROGRAM_DELAY 900\n"
                "REG_READ_DELAY 82\n"
                "CELL_READ_DELAY 50\n"
                "BLOCK_ERASE_DELAY 2000\n"
                "CHANNEL_SWITCH_DELAY_R 16\n"
                "CHANNEL_SWITCH_DELAY_W 33\n"
                "CHANNEL_NB %u\n"
                "STAT_TYPE 15\n"
                "STAT_SCOPE 62\n"
                "STAT_PATH /tmp/stat%u.csv\n"
                "STORAGE_STRATEGY %d\n"
                "GC_LOW_THR 20\n"
                "GC_HI_THR 80\n"
                "ONFI_MANAGER_THREADS 1\n"
                "ONFI_MANAGER_QUEUE_SIZE 1024\n",
                device + 1, device + 1, cfg->page_size,
                (unsigned long long) cfg->pages_per_block, cfg->dies_per_device,
                (unsigned long long) cfg->blocks_per_die, cfg->channels_per_device,
                device + 1, STRATEGY_SECTOR);
    }

    return fclose(file);
}
