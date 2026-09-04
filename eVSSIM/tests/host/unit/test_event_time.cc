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

#include <gtest/gtest.h>
#include <json.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fstream>
#include <iterator>
#include <string>

extern "C" {
#include "logging_parser.h"
}

namespace event_time_test {

    /**
     * The Go layout infra/ELK/filebeat.yml has to declare for its timestamp
     * processor: the reference time 2006-01-02 15:04:05 written the way
     * timestamp_to_str writes every timestamp. When the two drift apart
     * filebeat silently keeps ingest time as @timestamp and every time-derived
     * dashboard panel buckets on the wrong clock.
     */
    static const char GO_LAYOUT[] = "2006-01-02_15-04-05.000000";

    /**
     * Path of the filebeat config, relative to the test binary's folder
     * (eVSSIM/tests/host/unit).
     */
    static const char FILEBEAT_CONF[] = "../../../../infra/ELK/filebeat.yml";

    /**
     * An arbitrary but fixed event time: 2026-09-02 17:58:11.802030 UTC.
     */
    static const int64_t SAMPLE_TS_US = 1788371891802030LL;

    /**
     * Read a string property out of a serialized log line
     * @param json the log line produced by one of the JSON_X functions
     * @param key the property to read
     * @return the property value, or "" when it is missing
     */
    static std::string json_string_field(const char* json, const char* key) {
        struct json_object* jobj = json_tokener_parse(json);
        if (jobj == NULL)
            return "";

        struct json_object* value = NULL;
        std::string ret = "";
        if (json_object_object_get_ex(jobj, key, &value) && value != NULL)
            ret = std::string(json_object_get_string(value));

        json_object_put(jobj);
        return ret;
    }

    /**
     * The timestamp the logger writes must be readable with the layout filebeat
     * is configured with; otherwise @timestamp silently becomes ingest time.
     */
    TEST(EventTimeTest, EmittedTimestampMatchesFilebeatLayout) {
        char buf[TIME_STAMP_LEN];
        std::string emitted = std::string(timestamp_to_str(SAMPLE_TS_US, buf));

        struct tm parsed;
        memset(&parsed, 0, sizeof(parsed));
        const char* fraction = strptime(emitted.c_str(), "%Y-%m-%d_%H-%M-%S", &parsed);
        ASSERT_TRUE(fraction != NULL) << "not a <date>_<time> timestamp: " << emitted;
        EXPECT_EQ(std::string(".802030"), std::string(fraction))
            << "microseconds must be 6 digits to match " << GO_LAYOUT << ": " << emitted;

        std::ifstream conf(FILEBEAT_CONF);
        ASSERT_TRUE(conf.is_open()) << "cannot open " << FILEBEAT_CONF;
        std::string yaml((std::istreambuf_iterator<char>(conf)),
                         std::istreambuf_iterator<char>());
        EXPECT_NE(std::string::npos, yaml.find(GO_LAYOUT))
            << FILEBEAT_CONF << " does not declare the layout " << GO_LAYOUT;
    }

    /**
     * Garbage collection events used to ship without a timestamp of their own,
     * which left the "Garbage Collection Events Over Time" panel on ingest time.
     */
    TEST(EventTimeTest, GarbageCollectionLogShipsItsOwnEventTime) {
        char expected[TIME_STAMP_LEN];
        char* json = NULL;
        GarbageCollectionLog log;

        memset(&log, 0, sizeof(log));
        log.logging_time = SAMPLE_TS_US;
        JSON_GARBAGE_COLLECTION(&log, &json);
        ASSERT_TRUE(json != NULL);

        EXPECT_EQ(std::string(timestamp_to_str(SAMPLE_TS_US, expected)),
                  json_string_field(json, "logging_time"));
        free(json);
    }

    /**
     * The sync marker is shipped like any other event, so it needs a timestamp
     * too - without one it lands in the index at ingest time.
     */
    TEST(EventTimeTest, LogSyncShipsItsOwnEventTime) {
        char expected[TIME_STAMP_LEN];
        char* json = NULL;
        LoggeingServerSync log;

        memset(&log, 0, sizeof(log));
        log.log_id = 42;
        log.logging_time = SAMPLE_TS_US;
        JSON_LOG_SYNC(&log, &json);
        ASSERT_TRUE(json != NULL);

        EXPECT_EQ(std::string(timestamp_to_str(SAMPLE_TS_US, expected)),
                  json_string_field(json, "logging_time"));
        free(json);
    }

} // namespace event_time_test
