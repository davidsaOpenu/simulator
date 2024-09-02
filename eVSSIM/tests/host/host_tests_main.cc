#include "base_emulator_tests.h"

using namespace std;

bool g_ci_mode = false;
bool g_monitor_mode = false;
bool g_server_mode = false;

int main(int argc, char **argv) {
    string tests_filter = "*";

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ci") == 0) {
            g_ci_mode = true;
        } else if (strcmp(argv[i], "--show-monitor") == 0) {
            g_monitor_mode = true;
        } else if (strcmp(argv[i], "--run-server") == 0) {
            g_server_mode = true;
        } else if (strcmp(argv[i], "--sector-tests") == 0) {
            tests_filter = "*SectorUnitTest*";
        } else if (strcmp(argv[i], "--object-tests") == 0) {
            tests_filter = "*ObjectUnitTest*";
        } else if (strcmp(argv[i], "--log-mgr-tests") == 0) {
            tests_filter = "*LogMgrUnitTest*";
        } else if (strcmp(argv[i], "--ssd-io-emulator-tests") == 0) {
            tests_filter = "*SSDIoEmulatorUnitTest*";
        }
        else if (strcmp(argv[i], "--offline_logger_tests") == 0) {
            tests_filter = "*OfflineLoggerTest*";
        }
        else if (strcmp(argv[i], "--ssd_write_read_test") == 0) {
            tests_filter = "*WriteReadTest*";
        }
    }

    testing::GTEST_FLAG(filter) = tests_filter;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 