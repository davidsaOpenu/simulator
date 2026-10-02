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
#include "common.h"
}

#include <gtest/gtest.h>
#include <json.h>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace std;

namespace event_generator_tests {

const char* GENERATOR = "../../../../infra/ELK/event_generator.py";
const char* STREAM = "/tmp/event_stream.txt";
const char* TALLY = ELK_LOGGER_WRITER_LOGS_PATH "event_generator_tally.json";
const int DISKS = 3;

typedef map<string, vector<string> > DiskLines;       // disk -> log lines in file order
typedef map<string, map<string, int> > DiskTypeCounts; // disk -> event type -> count

static set<string> elk_log_files() {
    set<string> files;
    DIR* dir = opendir(ELK_LOGGER_WRITER_LOGS_PATH);
    if (dir == NULL)
        return files;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "elk_log_file-", strlen("elk_log_file-")) == 0)
            files.insert(ent->d_name);
    }
    closedir(dir);
    return files;
}

static string json_field(const string& line, const char* key) {
    struct json_object* obj = json_tokener_parse(line.c_str());
    struct json_object* field = NULL;
    string value = (obj != NULL && json_object_object_get_ex(obj, key, &field)) ? json_object_get_string(field) : "";
    json_object_put(obj);
    return value;
}

/** Replay the generator's stream through the real log manager: one logger and offline analyzer per disk */
static void replay() {
    Logger_Pool* loggers[DISKS];
    for (int d = 0; d < DISKS; d++)
        loggers[d] = logger_init(1);

    FILE* stream = fopen(STREAM, "r");
    ASSERT_TRUE(stream != NULL);
    char type[64];
    unsigned int disk, channel, die, block, page;
    long long start, end;
    while (fscanf(stream, "%u %63s %u %u %u %u %lld %lld", &disk, type, &channel, &die, &block, &page, &start, &end) == 8) {
        LogMetadata meta;
        memset(&meta, 0, sizeof(meta));
        meta.logging_start_time = start;
        meta.logging_end_time = end;
        if (strcmp(type, "PhysicalCellProgramLog") == 0) {
            PhysicalCellProgramLog log = { .channel = channel, .block = block, .page = page, .background = false, .metadata = meta };
            LOG_PHYSICAL_CELL_PROGRAM(loggers[disk], log);
        } else if (strcmp(type, "LogicalCellProgramLog") == 0) {
            LogicalCellProgramLog log = { .channel = channel, .block = block, .page = page, .metadata = meta };
            LOG_LOGICAL_CELL_PROGRAM(loggers[disk], log);
        } else if (strcmp(type, "PhysicalCellReadLog") == 0) {
            PhysicalCellReadLog log = { .channel = channel, .block = block, .page = page, .background = false, .metadata = meta };
            LOG_PHYSICAL_CELL_READ(loggers[disk], log);
        } else if (strcmp(type, "BlockEraseLog") == 0) {
            BlockEraseLog log = { .channel = channel, .die = die, .block = block, .dirty_page_nb = 0, .background = false, .metadata = meta };
            LOG_BLOCK_ERASE(loggers[disk], log);
        } else {
            ADD_FAILURE() << "unknown event type " << type;
        }
    }
    fclose(stream);

    elk_logger_writer_init();
    for (int d = 0; d < DISKS; d++) {
        OfflineLogAnalyzer* analyzer = offline_log_analyzer_init(loggers[d], d);
        // exit flag raised before the loop runs: it drains the whole queue once and returns
        analyzer->exit_loop_flag = 1;
        offline_log_analyzer_loop(analyzer);
        offline_log_analyzer_free(analyzer);
        logger_free(loggers[d]);
    }
    elk_logger_writer_free();
}

/** Generate the stream, replay it, return the log lines this run wrote */
static DiskLines run() {
    set<string> before = elk_log_files();
    EXPECT_EQ(0, system((string("python3 ") + GENERATOR + " " + STREAM + " " + TALLY).c_str()));
    replay();

    DiskLines lines;
    set<string> after = elk_log_files();
    for (set<string>::iterator f = after.begin(); f != after.end(); ++f) {
        if (before.count(*f))
            continue;
        ifstream file((string(ELK_LOGGER_WRITER_LOGS_PATH) + *f).c_str());
        string line;
        while (getline(file, line))
            lines[json_field(line, "device_index")].push_back(line);
    }
    return lines;
}

TEST(EventGeneratorTest, TallyMatchesLogLines) {
    DiskLines lines = run();

    DiskTypeCounts logged;
    for (DiskLines::iterator d = lines.begin(); d != lines.end(); ++d)
        for (size_t i = 0; i < d->second.size(); i++)
            logged[d->first][json_field(d->second[i], "type")]++;

    DiskTypeCounts tally;
    struct json_object* root = json_object_from_file(TALLY);
    ASSERT_TRUE(root != NULL);
    json_object_object_foreach(root, disk, types) {
        json_object_object_foreach(types, type, count)
            tally[disk][type] = json_object_get_int(count);
    }
    json_object_put(root);

    ASSERT_EQ((size_t)DISKS, tally.size());
    for (DiskTypeCounts::iterator d = tally.begin(); d != tally.end(); ++d)
        for (map<string, int>::iterator t = d->second.begin(); t != d->second.end(); ++t)
            EXPECT_EQ(t->second, logged[d->first][t->first]) << "disk " << d->first << " " << t->first;
    // no disk or event type that the generator did not emit
    EXPECT_EQ(tally, logged);
}

TEST(EventGeneratorTest, Deterministic) {
    DiskLines first = run();
    DiskLines second = run();

    ASSERT_EQ((size_t)DISKS, first.size());
    ASSERT_EQ(first.size(), second.size());
    for (DiskLines::iterator d = first.begin(); d != first.end(); ++d) {
        ASSERT_EQ(d->second.size(), second[d->first].size()) << "disk " << d->first;
        for (size_t i = 0; i < d->second.size(); i++)
            ASSERT_EQ(d->second[i], second[d->first][i]) << "disk " << d->first << " line " << i;
    }
}
} //namespace
