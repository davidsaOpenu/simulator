#include <gtest/gtest.h>

using namespace std;

int main(int argc, char **argv)
{
    string tests_filter = "*";

    testing::GTEST_FLAG(filter) = tests_filter;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
