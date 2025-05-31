#include <gtest/gtest.h>

using namespace std;

int main(int argc, char **argv) {
    string tests_filter = "*";

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ssd-file-ops") == 0) {
            tests_filter = "*SsdFileOpsTest*";
        }
    }

    testing::GTEST_FLAG(filter) = tests_filter;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 
