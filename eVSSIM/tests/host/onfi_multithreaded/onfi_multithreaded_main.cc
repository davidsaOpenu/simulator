#include <cstdio>
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

static void remove_args(int *argc, char **argv, int start, int count)
{
    for (int j = start; j + count < *argc; j++)
        argv[j] = argv[j + count];
    *argc -= count;
}

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

/*
 * If argv[i] is `name` (with the value in argv[i+1]) or `name=value`, map
 * `mt` to 1 and `serial` to 0 in *out and return the number of argv entries
 * consumed (2 or 1).
 * Returns 0 when argv[i] does not match.
 */
static int parse_mode_opt(int argc, char **argv, int i, const char *name, long *out)
{
    const size_t n = strlen(name);
    const char *value = NULL;
    int consumed;

    if (strcmp(argv[i], name) == 0) {
        if (i + 1 >= argc) {
            return 0;
        }
        value = argv[i + 1];
        consumed = 2;
    } else if (strncmp(argv[i], name, n) == 0 && argv[i][n] == '=') {
        value = argv[i] + n + 1;
        consumed = 1;
    } else {
        return 0;
    }

    if (strcmp(value, "mt") == 0) {
        *out = 1;
        return consumed;
    }
    if (strcmp(value, "serial") == 0) {
        *out = 0;
        return consumed;
    }

    fprintf(stderr, "ERROR: %s must be mt or serial, got '%s'\n", name, value);
    return 0;
}

int main(int argc, char **argv)
{
    bool mode_flag_seen = false;

    for (int i = 1; i < argc; ) {
        long value = 0;
        int consumed = 0;

        if ((consumed = parse_mode_opt(argc, argv, i, "--mode", &value)) != 0) {
            g_onfi_mt = value ? 1 : 0;
            mode_flag_seen = true;
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

    if (!mode_flag_seen) {
        fprintf(stderr, "ERROR: --mode is required (mt/serial)\n");
        return 1;
    }

    testing::GTEST_FLAG(filter) = "*";
    testing::InitGoogleTest(&argc, argv);
    int ret = RUN_ALL_TESTS();
    LOG_MANAGER_WAIT_UNTIL_ALL_SHIPPED(600000);
    return ret;
}
