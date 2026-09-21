#include <gtest/gtest.h>
#include <cstdio>

extern "C" {
#include "ssd_log_manager.h"
}

using namespace std;

int main(int argc, char **argv)
{
    string tests_filter = "*";

    testing::GTEST_FLAG(filter) = tests_filter;
    testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    LOG_MANAGER_WAIT_UNTIL_ALL_SHIPPED(600000);
    return ret;
}
