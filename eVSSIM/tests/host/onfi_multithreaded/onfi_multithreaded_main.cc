#include <cstdlib>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

extern "C" {
#include "ssd_log_manager.h"
}

int g_onfi_mt = 0;
int g_page_nb = 64;
int g_block_nb = 4096;
int g_flash_nb = 8;
long g_ops = 1000000;

/*
 * If argv[i] is `name` (with the value in argv[i+1]) or `name=value`, parse the
 * value into *out and return the number of argv entries consumed (2 or 1).
 * Returns 0 when argv[i] does not match.
 */
static int parse_int_opt(int argc, char **argv, int i, const char *name, long *out)
{
    const size_t n = strlen(name);

    if (strcmp(argv[i], name) == 0) {
        if (i + 1 >= argc)
            return 0;
        *out = strtol(argv[i + 1], NULL, 10);
        return 2;
    }
    if (strncmp(argv[i], name, n) == 0 && argv[i][n] == '=') {
        *out = strtol(argv[i] + n + 1, NULL, 10);
        return 1;
    }
    return 0;
}

static void remove_args(int *argc, char **argv, int start, int count)
{
    for (int j = start; j + count < *argc; j++)
        argv[j] = argv[j + count];
    *argc -= count;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ) {
        long value = 0;
        int consumed = 0;

        if ((consumed = parse_int_opt(argc, argv, i, "--onfi-multithreaded", &value)) != 0) {
            g_onfi_mt = value ? 1 : 0;
        } else if ((consumed = parse_int_opt(argc, argv, i, "--pages", &value)) != 0) {
            if (value > 0)
                g_page_nb = (int)value;
        } else if ((consumed = parse_int_opt(argc, argv, i, "--blocks", &value)) != 0) {
            if (value > 0)
                g_block_nb = (int)value;
        } else if ((consumed = parse_int_opt(argc, argv, i, "--flashes", &value)) != 0) {
            if (value > 0)
                g_flash_nb = (int)value;
        } else if ((consumed = parse_int_opt(argc, argv, i, "--ops", &value)) != 0) {
            if (value > 0)
                g_ops = value;
        } else {
            i++;
            continue;
        }

        /* Remove the consumed option so gtest does not see it. */
        remove_args(&argc, argv, i, consumed);
    }

    testing::GTEST_FLAG(filter) = "*";
    testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    LOG_MANAGER_WAIT_UNTIL_ALL_SHIPPED(600000);
    return ret;
}
