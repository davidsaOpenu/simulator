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
        }
        else if (strcmp(argv[i], "--show-monitor") == 0) {
            g_monitor_mode = true;
        }
        else if (strcmp(argv[i], "--run-server") == 0) {
            g_server_mode = true;
        }
        else if (strcmp(argv[i], "--sector-tests") == 0) {
            tests_filter = "*SectorUnitTest*";
        }
        else if (strcmp(argv[i], "--object-tests") == 0) {
            tests_filter = "*ObjectUnitTest*";
        }
        else if (strcmp(argv[i], "--log-mgr-tests") == 0) {
            tests_filter = "*LogMgrUnitTest*";
        }
        else if (strcmp(argv[i], "--ssd-io-emulator-tests") == 0) {
            tests_filter = "*SSDIoEmulatorUnitTest*";
        }
        else if (strcmp(argv[i], "--offline_logger_tests") == 0) {
            tests_filter = "*OfflineLoggerTest*";
        }
        else if (strcmp(argv[i], "--ssd_write_read_test") == 0) {
            tests_filter = "*WriteReadTest*";
        }
        else if (strcmp(argv[i], "--ssd_program_compatible_test") == 0) {
            tests_filter = "*ProgramCompatibleTest*";
        }
        else if (strcmp(argv[i], "--onfi_ops_test") == 0) {
            tests_filter = "*OnfiCommandsTest*";
        }
        else if (strcmp(argv[i], "--multi_device_tests") == 0) {
            tests_filter = "*MultiDevice*";
        }
    }

    testing::GTEST_FLAG(filter) = tests_filter;
    testing::InitGoogleTest(&argc, argv);
    int rc = RUN_ALL_TESTS();

    // Reconciliation manifest: events produced this run; the orchestrator waits
    // until ELK holds exactly this many for EVSSIM_RUN_ID before asserting.
    const char* run_id = getenv("EVSSIM_RUN_ID");
    printf("EVSSIM_RUN_MANIFEST run_id=%s events=%llu\n",
           run_id ? run_id : "",
           (unsigned long long)SSD_GET_RUN_EVENT_COUNT());
    fflush(stdout);

    return rc;
}
