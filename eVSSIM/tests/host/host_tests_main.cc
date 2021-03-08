#include "base_emulator_tests.h"
#include "log_mgr_tests.h"

bool g_ci_mode = false;
bool g_monitor_mode = false;
bool g_server_mode = false;
bool g_nightly_mode = false;

int main(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ci") == 0) {
            g_ci_mode = true;
        } else if (strcmp(argv[i], "--show-monitor") == 0) {
            g_monitor_mode = true;
        } else if (strcmp(argv[i], "--run-server") == 0) {
            g_server_mode = true;
        } else if (strcmp(argv[i], "--nightly") == 0) {
            g_nightly_mode = true;
        }
    }

    // check if CI_MODE environment variable is set to NIGHTLY
    const char *ci_mode = getenv("CI_MODE");
    if (ci_mode != NULL && std::string(ci_mode) == "NIGHTLY") {
        g_nightly_mode = true;
    }

    testing::InitGoogleTest(&argc, argv);
    log_mgr_tests::init();
    return RUN_ALL_TESTS();
}
