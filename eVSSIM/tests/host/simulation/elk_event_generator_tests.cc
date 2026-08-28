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

extern "C" {
#include "elk_event_generator.h"
}

#define GTEST_DONT_DEFINE_FAIL 1
#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace elk_event_generator_tests {

    /**
     * Serialize an op as text: the emitted events carry wall-clock timestamps, so the
     * stream that has to be reproducible is the plan, not the log lines it produces.
     */
    void append_op(const elk_gen_op* op, void* ctx) {
        char line[128];
        snprintf(line, sizeof(line), "%u %u %u %llu %llu %d\n",
                 (unsigned int) op->device_index, op->channel, op->die,
                 (unsigned long long) op->block, (unsigned long long) op->page,
                 (int) op->type);
        ((std::string*) ctx)->append(line);
    }

    void collect_op(const elk_gen_op* op, void* ctx) {
        ((std::vector<elk_gen_op>*) ctx)->push_back(*op);
    }

    size_t count_ops(const std::string& stream) {
        return (size_t) std::count(stream.begin(), stream.end(), '\n');
    }

    elk_gen_config test_config(uint64_t seed) {
        elk_gen_config cfg;
        elk_gen_default_config(&cfg);
        cfg.seed = seed;
        /* the plan tests do not need the whole tier to show the plan is reproducible */
        cfg.total_bytes = 512 * 1024;
        return cfg;
    }

    /**
     * The whole point of the generator: a run can be replayed, so a gate can compare
     * against ground truth computed from the plan.
     */
    TEST(ElkEventGeneratorTest, SameSeedProducesIdenticalStream) {
        elk_gen_config cfg = test_config(20587);
        std::string first, second;

        elk_gen_walk(&cfg, append_op, &first);
        elk_gen_walk(&cfg, append_op, &second);

        ASSERT_FALSE(first.empty());
        ASSERT_EQ(first, second);
    }

    /**
     * Guards the determinism test above against passing on a constant stream.
     */
    TEST(ElkEventGeneratorTest, DifferentSeedProducesDifferentStream) {
        elk_gen_config one = test_config(20587);
        elk_gen_config other = test_config(20588);
        std::string first, second;

        elk_gen_walk(&one, append_op, &first);
        elk_gen_walk(&other, append_op, &second);

        ASSERT_EQ(count_ops(first), count_ops(second));
        ASSERT_NE(first, second);
    }

} //namespace

namespace elk_event_generator_tests {

    /**
     * The plan is split per (channel, die) writer from the first commit, so that turning
     * the writers into threads later does not move a single event to another die.
     */
    TEST(ElkEventGeneratorTest, EveryWriterKeepsToOneDieAndItsChannel) {
        elk_gen_config cfg = test_config(20587);
        std::set<std::pair<unsigned int, unsigned int> > seen;
        uint64_t writers = elk_gen_writer_count(&cfg);
        uint64_t writer;

        ASSERT_EQ((uint64_t) cfg.device_count * cfg.dies_per_device, writers);

        for (writer = 0; writer < writers; writer++) {
            std::vector<elk_gen_op> ops;
            elk_gen_walk_writer(&cfg, writer, collect_op, &ops);

            ASSERT_FALSE(ops.empty()) << "writer " << writer << " got no events";
            for (size_t i = 0; i < ops.size(); i++) {
                EXPECT_EQ(ops[0].device_index, ops[i].device_index);
                EXPECT_EQ(ops[0].die, ops[i].die);
                EXPECT_EQ(ops[i].die % cfg.channels_per_device, ops[i].channel);
            }
            seen.insert(std::make_pair((unsigned int) ops[0].device_index, ops[0].die));
        }

        ASSERT_EQ((size_t) writers, seen.size());
    }

    /**
     * A writer's stream does not depend on the writers running beside it: this is what
     * lets the parallel run of story 8 be checked against the same ground truth.
     */
    TEST(ElkEventGeneratorTest, WalkingWritersOneByOneReproducesTheWholePlan) {
        elk_gen_config cfg = test_config(20587);
        std::string whole, joined;
        uint64_t writers = elk_gen_writer_count(&cfg);
        uint64_t writer;

        elk_gen_walk(&cfg, append_op, &whole);
        for (writer = 0; writer < writers; writer++)
            elk_gen_walk_writer(&cfg, writer, append_op, &joined);

        ASSERT_EQ(whole, joined);
    }

    /**
     * The volume tier is spent one page sized event at a time.
     */
    TEST(ElkEventGeneratorTest, PlanHoldsOneEventPerPageOfTheTier) {
        elk_gen_config cfg = test_config(20587);
        std::string stream;

        elk_gen_walk(&cfg, append_op, &stream);

        ASSERT_EQ((size_t) (cfg.total_bytes / cfg.page_size), count_ops(stream));
    }

} //namespace

extern "C" {
#include "common.h"
#include "test_context.h"
}

#include <dirent.h>
#include <json.h>
#include <unistd.h>
#include <fstream>
#include <map>
#include <tuple>

namespace elk_event_generator_tally_tests {

    using elk_event_generator_tests::test_config;

    #define GENERATOR_TALLY_PATH ELK_LOGGER_WRITER_LOGS_PATH "elk_generator_tally_test.json"
    #define LOG_FILE_PREFIX "elk_log_file-"

    /**
     * The events written by one run, keyed by "<device index> <log type>"
     */
    typedef std::map<std::string, uint64_t> event_counts;

    std::string count_key(int device_index, const std::string& type) {
        char key[160];
        snprintf(key, sizeof(key), "%d %s", device_index, type.c_str());
        return std::string(key);
    }

    /**
     * The log files present right now
     */
    std::set<std::string> log_files_now() {
        std::set<std::string> files;
        DIR* dir = opendir(ELK_LOGGER_WRITER_LOGS_PATH);
        struct dirent* ent;

        if (dir == NULL)
            return files;
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, LOG_FILE_PREFIX, sizeof(LOG_FILE_PREFIX) - 1) == 0)
                files.insert(std::string(ent->d_name));
        }
        closedir(dir);

        return files;
    }

    /**
     * Count the event lines of a single run in the log files
     * @param uuid the run uuid the lines must carry
     * @param before the log files that already existed when the run started; the writer
     *        opens a fresh file for the run, so the older ones cannot hold its events,
     *        and skipping them keeps the test off every earlier run in the directory
     */
    event_counts count_logged_events(const char* uuid, const std::set<std::string>& before) {
        event_counts counts;
        DIR* dir = opendir(ELK_LOGGER_WRITER_LOGS_PATH);
        struct dirent* ent;

        if (dir == NULL)
            return counts;

        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, LOG_FILE_PREFIX, sizeof(LOG_FILE_PREFIX) - 1) != 0)
                continue;
            if (before.find(std::string(ent->d_name)) != before.end())
                continue;

            std::ifstream log_file((std::string(ELK_LOGGER_WRITER_LOGS_PATH) + ent->d_name).c_str());
            std::string line;

            while (getline(log_file, line)) {
                json_object* parsed = json_tokener_parse(line.c_str());
                json_object *type_obj, *uuid_obj, *device_obj;

                if (parsed == NULL)
                    continue;
                if (json_object_object_get_ex(parsed, "test.uuid", &uuid_obj) &&
                    strcmp(json_object_get_string(uuid_obj), uuid) == 0 &&
                    json_object_object_get_ex(parsed, "type", &type_obj) &&
                    json_object_object_get_ex(parsed, "device_index", &device_obj)) {
                    counts[count_key(json_object_get_int(device_obj),
                                     json_object_get_string(type_obj))]++;
                }
                json_object_put(parsed);
            }
        }
        closedir(dir);

        return counts;
    }

    /**
     * Read back the tally the generator wrote next to the logs
     */
    event_counts read_tally_file(const char* path) {
        event_counts counts;
        json_object* parsed = json_object_from_file(path);
        json_object* devices_obj;
        int i, type;

        if (parsed == NULL)
            return counts;

        if (json_object_object_get_ex(parsed, "devices", &devices_obj)) {
            for (i = 0; i < (int) json_object_array_length(devices_obj); i++) {
                json_object* device_obj = json_object_array_get_idx(devices_obj, i);
                json_object *index_obj, *count_obj;

                if (!json_object_object_get_ex(device_obj, "device_index", &index_obj))
                    continue;
                for (type = 0; type < ELK_GEN_OP_COUNT; type++) {
                    const char* name = elk_gen_log_type_name((elk_gen_op_type) type);
                    if (json_object_object_get_ex(device_obj, name, &count_obj))
                        counts[count_key(json_object_get_int(index_obj), name)] =
                            json_object_get_int64(count_obj);
                }
            }
        }
        json_object_put(parsed);

        return counts;
    }

    /**
     * The tally is the ground truth the ELK gates are checked against, so it has to hold
     * exactly the events that reached the log files, per event type and per disk.
     */
    TEST(ElkEventGeneratorTallyTest, TallyMatchesTheEventLinesWrittenToTheLogFiles) {
        elk_gen_config cfg = test_config(20587);
        test_execution_context_t ctx;
        elk_gen_tally tally;
        struct timeval now;
        char uuid[64];
        uint8_t device;

        /* a small tier: this test pays for the whole log pipeline, not just the plan.
           still large enough for every writer to get events of all three types */
        cfg.total_bytes = 2 * 1024 * 1024;

        /* the run needs an identity of its own: the log directory keeps the files of
           earlier runs, and a pid comes back around. get_usec() is the simulated clock
           and reads 0 before the disks are up, so the wall clock is what serves here */
        gettimeofday(&now, NULL);
        snprintf(uuid, sizeof(uuid), "elk-gen-tally-%d-%lld", (int) getpid(),
                 (long long) now.tv_sec * 1000000LL + now.tv_usec);
        memset(&ctx, 0, sizeof(ctx));
        ctx.test_name = "TallyMatchesTheEventLinesWrittenToTheLogFiles";
        ctx.test_case_name = "ElkEventGeneratorTallyTest";
        ctx.test_run_uuid = uuid;

        std::set<std::string> log_files_before = log_files_now();

        ASSERT_EQ(0, elk_gen_write_ssd_conf(&cfg, "data/ssd.conf"));
        SSD_SET_TEST_CONTEXT(&ctx);
        ASSERT_EQ(0, elk_gen_devices_up(&cfg));

        elk_gen_tally_reset(&tally);
        elk_gen_walk(&cfg, elk_gen_emit, &tally);

        elk_gen_devices_down(&cfg);
        SSD_CLEAR_TEST_CONTEXT();
        /* the config parser creates a data directory per disk; leaving them behind
           skews the suites that run after this one */
        for (device = 0; device < cfg.device_count; device++)
            std::ignore = system((std::string("rm -rf data/") + std::to_string(device)).c_str());

        ASSERT_EQ(0, elk_gen_tally_write(&tally, &cfg, GENERATOR_TALLY_PATH));
        event_counts tallied = read_tally_file(GENERATOR_TALLY_PATH);
        event_counts logged = count_logged_events(uuid, log_files_before);
        unlink(GENERATOR_TALLY_PATH);

        /* the ssd module emits more than the three types the generator claims: the
           register, channel switch, logical program and utilization events are the
           ground truth of later stories, and are not counted here */
        ASSERT_EQ((size_t) (cfg.device_count * ELK_GEN_OP_COUNT), tallied.size());
        for (event_counts::iterator it = tallied.begin(); it != tallied.end(); ++it) {
            EXPECT_GT(it->second, (uint64_t) 0) << "nothing tallied for " << it->first;
            EXPECT_EQ(it->second, logged[it->first]) << "mismatch for " << it->first;
        }
    }

} //namespace
