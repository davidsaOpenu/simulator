#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "onfi.h"
#include "vssim_config_manager.h"
#include "ftl.h"
#include "ssd_log_manager.h"
}

#include "test_context.h"
#include <gtest/gtest.h>


/* Set by onfi_multithreaded_main.cc from the command line. */
extern int g_onfi_mt;
extern int g_page_nb;
extern int g_block_nb;
extern int g_flash_nb;
extern long g_ops;

namespace onfi_performance_test
{

    static const int PAGE_SIZE = 4096;
    static const int QUEUE_SIZE = 8192;

    /* Fixed seed so serial and multithreaded runs execute the same sequence. */
    static const unsigned int RANDOM_SEED = 0;

    /* Operations submitted before waiting, so memory stays bounded while the
     * per-flash queues still stay busy. */
    static const int BATCH_OPS = 2048;

    /* Operation mix, in percent. The remainder is block erases. */
    static const int READ_PCT = 60;
    static const int PROGRAM_PCT = 35;
    // static const int ERASE_PCT = 5;

    enum OpType { OP_READ, OP_PROGRAM, OP_ERASE };

    struct Op {
        uint64_t ppn;
        OpType type;
    };

    static void write_config_file(int multithreaded)
    {
        std::FILE *ssd_conf = fopen("data/ssd.conf", "w");
        if (ssd_conf == NULL) {
            PERR("Can't open file: ./data/ssd.conf\n");
            return;
        }

        /* One channel per flash chip: each flash is fully independent, which
         * is what the per-flash worker threads model. */
        fprintf(ssd_conf,
            "[nvme01]\n"
            "PAGE_SIZE %d\n"
            "PAGE_NB %d\n"
            "SECTOR_SIZE 1\n"
            "FLASH_NB %d\n"
            "BLOCK_NB %d\n"
            "PLANES_PER_FLASH 1\n"
            "REG_WRITE_DELAY 82\n"
            "CELL_PROGRAM_DELAY 900\n"
            "REG_READ_DELAY 82\n"
            "CELL_READ_DELAY 50\n"
            "BLOCK_ERASE_DELAY 2000\n"
            "CHANNEL_SWITCH_DELAY_R 16\n"
            "CHANNEL_SWITCH_DELAY_W 33\n"
            "CHANNEL_NB %d\n"
            "STAT_TYPE 15\n"
            "STAT_SCOPE 62\n"
            "STAT_PATH /tmp/onfi_random_stat.csv\n"
            "STORAGE_STRATEGY 1\n"
            "GC_LOW_THR 20\n"
            "GC_HI_THR 80\n"
            "ONFI_MULTITHREADED %d\n"
            "ONFI_MANAGER_QUEUE_SIZE %d\n",
            PAGE_SIZE, g_page_nb, g_flash_nb, g_block_nb, g_flash_nb,
            multithreaded, QUEUE_SIZE);

        fclose(ssd_conf);
    }

    class OnfiPerformanceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            write_config_file(g_onfi_mt);
            INIT_SSD_CONFIG();

            test_execution_context_t ctx;
            memset(&ctx, 0, sizeof(ctx));
            ctx.test_name = "OnfiPerformanceTest";
            ctx.test_case_name = "OnfiPerformanceTest";
            ctx.test_run_uuid = g_onfi_mt ? "onfi_mt" : "onfi_serial";
            ctx.ssd_total_size_bytes =
                (uint64_t)PAGE_SIZE * g_page_nb * g_block_nb * g_flash_nb;
            ctx.test_start_timestamp_us = 0;
            SSD_SET_TEST_CONTEXT(&ctx);

            FTL_INIT(g_device_index);
            INIT_LOG_MANAGER(g_device_index);

            /* Keep the GC thread out while we drive ONFI directly. */
            LOCK_DEVICE(g_device_index);

            ASSERT_EQ(ONFI_INIT(g_device_index), ONFI_SUCCESS);
        }

        void TearDown() override
        {
            UNLOCK_DEVICE(g_device_index);
            FTL_TERM(g_device_index);
            SSD_CLEAR_TEST_CONTEXT();

            std::string cleanup = std::string("rm -rf data/") +
                                  std::to_string(g_device_index);
            if (system(cleanup.c_str()) != 0) {
                /* best-effort cleanup */
            }

            TERM_LOG_MANAGER(g_device_index);
            TERM_SSD_CONFIG();
        }
    };

    /*
     * Submit `g_ops` randomly addressed operations (read/program/erase mix)
     * across all flash chips and verify that every one completes successfully
     * with the expected transfer size.
     */
    TEST_F(OnfiPerformanceTest, RandomWorkload)
    {
        const uint64_t total_pages =
            (uint64_t)g_page_nb * g_block_nb * g_flash_nb;

        printf("Test params: flashes=%d blocks=%d pages=%d total_pages=%llu ops=%ld mode=%s\n",
               g_flash_nb, g_block_nb, g_page_nb, (unsigned long long)total_pages, g_ops, g_onfi_mt ? "mt" : "serial");
        fflush(stdout);

        /* Generate the operation sequence once from the fixed seed so both
         * dispatch modes execute exactly the same work. */
        std::vector<Op> ops((size_t)g_ops);
        unsigned int state = RANDOM_SEED;
        long n_read = 0, n_program = 0, n_erase = 0;
        for (long i = 0; i < g_ops; i++) {
            ops[(size_t)i].ppn = (uint64_t)rand_r(&state) % total_pages;

            int random_num = rand_r(&state) % 100;
            if (random_num < READ_PCT) {
                ops[(size_t)i].type = OP_READ;
                n_read++;
            } else if (random_num < READ_PCT + PROGRAM_PCT) {
                ops[(size_t)i].type = OP_PROGRAM;
                n_program++;
            } else {
                ops[(size_t)i].type = OP_ERASE;
                n_erase++;
            }
        }

        std::vector<uint8_t> prog_buf((size_t)BATCH_OPS * PAGE_SIZE, 0);
        std::vector<uint8_t> read_buf((size_t)BATCH_OPS * PAGE_SIZE, 0);
        std::vector<size_t> xfer((size_t)BATCH_OPS, 0);
        std::vector<onfi_handle_t *> handles;
        handles.reserve((size_t)BATCH_OPS);

        long errors = 0;
        for (long batch_start = 0; batch_start < g_ops; batch_start += BATCH_OPS) {
            long batch_ops = g_ops - batch_start;
            if (batch_ops > BATCH_OPS)
                batch_ops = BATCH_OPS;

            // Run a batch of onfi operations
            handles.clear();
            for (long i = 0; i < batch_ops; i++) {
                const Op &op = ops[(size_t)(batch_start + i)];
                uint8_t *pbuf = prog_buf.data() + (size_t)i * PAGE_SIZE;
                uint8_t *rbuf = read_buf.data() + (size_t)i * PAGE_SIZE;

                /* Known data pattern derived from the target page number. */
                memset(pbuf, (uint8_t)(op.ppn & 0xFF), PAGE_SIZE);
                memset(rbuf, 0, PAGE_SIZE);
                xfer[(size_t)i] = 0;

                switch (op.type) {
                case OP_READ:
                    handles.push_back(ONFI_READ(g_device_index, op.ppn, 0, rbuf,
                        PAGE_SIZE, &xfer[(size_t)i], 0, READ));
                    break;
                case OP_PROGRAM:
                    handles.push_back(ONFI_PAGE_PROGRAM(g_device_index, op.ppn, 0,
                        pbuf, PAGE_SIZE, &xfer[(size_t)i], 0, WRITE));
                    break;
                case OP_ERASE:
                    handles.push_back(ONFI_BLOCK_ERASE(g_device_index, op.ppn, ERASE));
                    break;
                }
            }

            // Call onfi wait on the batch onfi operation
            for (long i = 0; i < batch_ops; i++) {
                onfi_handle_t *h = handles[(size_t)i];
                if (h == NULL) {
                    errors++;
                    continue;
                }
                if (ONFI_WAIT(h) != ONFI_SUCCESS) {
                    errors++;
                    continue;
                }
                if (ops[(size_t)(batch_start + i)].type != OP_ERASE &&
                    xfer[(size_t)i] != (size_t)PAGE_SIZE) {
                    errors++;
                }
            }
        }

        printf("Test finished: ops=%ld errors=%ld\n", g_ops, errors);
        fflush(stdout);
        EXPECT_EQ(errors, 0L) << "random workload reported failures";
    }

} /* namespace onfi_performance_test */
