/*
 * Copyright 2023 The Open University of Israel
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

#include <cmath>
#include "base_emulator_tests.h"

extern RTLogStatistics *rt_log_stats;
extern LogServer log_server;

// for performance.
#define CHECK_THRESHOLD 0.1 // 10%

using namespace std;

namespace program_compatible_test
{

    class OnfiCommandsTest : public BaseTest
    {
    public:
        virtual void SetUp()
        {
            BaseTest::SetUp();
            INIT_LOG_MANAGER();
            ASSERT_EQ(_FTL_CREATE(), FTL_SUCCESS);
        }

        virtual void TearDown()
        {
            BaseTest::TearDown();
            TERM_LOG_MANAGER();
            remove(GET_FILE_NAME());
        }
    };

    std::vector<SSDConf *> GetTestParams()
    {
        std::vector<SSDConf *> ssd_configs;
        ssd_configs.push_back(new SSDConf(pow(2, 5), 1));
        ssd_configs.push_back(new SSDConf(pow(2, 5), 2));
        return ssd_configs;
    }

    INSTANTIATE_TEST_CASE_P(DiskSize, OnfiCommandsTest, ::testing::ValuesIn(GetTestParams()));

    TEST_P(OnfiCommandsTest, Try)
    {

        ASSERT_EQ(1, 1);

    }

} // namespace
