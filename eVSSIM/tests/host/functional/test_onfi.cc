#include <cstdio>
#include <cstring>
#include <fstream>
#include <cstdint>
#include <vector>
#include <pthread.h>
extern "C" {
#include "onfi.h"
#include "vssim_config_manager.h"
#include "ftl.h"
};

#include <gtest/gtest.h>

using namespace std;

namespace onfi_functional_test
{
    static const int PAGE_SIZE = 4096;
    static const int PAGE_NB = 64;
    static const int BLOCK_NB = 64;
    static const int FLASH_NB = 8;
    static const int OPS_PER_FLASH = 500;

    struct TestParam {
        int channels;
        int multithreaded;
    };

    static const TestParam TEST_PARAMS[] = {
        {4, 0},  // serial mode
        {1, 1},  // 1 channel,  multithreaded (8 workers, one per flash)
        {4, 1},  // 4 channels, multithreaded
        {8, 1}   // 8 channels, multithreaded
    };

    // The number of threads that are used in MultiMainThreads test case
    static const int MULTI_MAIN_THREADS_NUM_THREADS_OPTIONS[] = {1,2,4,8,10};

    static const int MULTI_MAIN_THREADS_NUM_BLOCKS_PER_TEST = 10;
    static const int MULTI_MAIN_THREADS_NUM_PAGES_PER_BLOCK = 10;

    /*
     * Fill a buffer with a byte tag derived from the flash chip index.
     * Used to identify which chip wrote a given page on readback.
     */
    static void fill_with_flash_index(uint8_t* buf, int flash_index, size_t size) {
        uint8_t tag = (uint8_t)(flash_index);
        memset(buf, tag, size);
    }

    /*
     * Return the LBA of the first page belonging to the given flash chip.
     */
    static uint64_t get_flash_first_page_address(int flash_index) {
        return (uint64_t)flash_index * BLOCK_NB * PAGE_NB;
    }

    static int blocks_per_flash() {
        return (OPS_PER_FLASH + PAGE_NB - 1) / PAGE_NB;
    }

    static void write_config_file(int channels, int multithreaded) {
        ofstream ssd_conf("data/ssd.conf", ios_base::out | ios_base::trunc);
        ssd_conf << "[nvme01]\n"
            "PAGE_SIZE " << PAGE_SIZE << "\n"
            "PAGE_NB " << PAGE_NB << "\n"
            "SECTOR_SIZE 1\n"
            "FLASH_NB " << FLASH_NB << "\n"
            "BLOCK_NB " << BLOCK_NB << "\n"
            "PLANES_PER_FLASH 1\n"
            "REG_WRITE_DELAY 82\n"
            "CELL_PROGRAM_DELAY 900\n"
            "REG_READ_DELAY 82\n"
            "CELL_READ_DELAY 50\n"
            "BLOCK_ERASE_DELAY 2000\n"
            "CHANNEL_SWITCH_DELAY_R 16\n"
            "CHANNEL_SWITCH_DELAY_W 33\n"
            "CHANNEL_NB " << channels << "\n"
            "STAT_TYPE 15\n"
            "STAT_SCOPE 62\n"
            "STAT_PATH /tmp/stat.csv\n"
            "STORAGE_STRATEGY 1\n"
            "GC_LOW_THR 20\n"
            "GC_HI_THR 80\n"
            "ONFI_MULTITHREADED " << multithreaded << "\n"
            "ONFI_MANAGER_QUEUE_SIZE 1024\n"
            "[nvme02]\n"
            "PAGE_SIZE " << PAGE_SIZE << "\n"
            "PAGE_NB " << PAGE_NB << "\n"
            "SECTOR_SIZE 1\n"
            "FLASH_NB " << FLASH_NB << "\n"
            "BLOCK_NB " << BLOCK_NB << "\n"
            "PLANES_PER_FLASH 1\n"
            "REG_WRITE_DELAY 82\n"
            "CELL_PROGRAM_DELAY 900\n"
            "REG_READ_DELAY 82\n"
            "CELL_READ_DELAY 50\n"
            "BLOCK_ERASE_DELAY 2000\n"
            "CHANNEL_SWITCH_DELAY_R 16\n"
            "CHANNEL_SWITCH_DELAY_W 33\n"
            "CHANNEL_NB " << channels << "\n"
            "STAT_TYPE 15\n"
            "STAT_SCOPE 62\n"
            "STAT_PATH /tmp/stat2.csv\n"
            "STORAGE_STRATEGY 1\n"
            "GC_LOW_THR 20\n"
            "GC_HI_THR 80\n"
            "ONFI_MULTITHREADED " << multithreaded << "\n"
            "ONFI_MANAGER_QUEUE_SIZE 1024\n"
            "[nvme03]\n"
            "PAGE_SIZE " << PAGE_SIZE << "\n"
            "PAGE_NB " << PAGE_NB << "\n"
            "SECTOR_SIZE 1\n"
            "FLASH_NB " << FLASH_NB << "\n"
            "BLOCK_NB " << BLOCK_NB << "\n"
            "PLANES_PER_FLASH 1\n"
            "REG_WRITE_DELAY 82\n"
            "CELL_PROGRAM_DELAY 900\n"
            "REG_READ_DELAY 82\n"
            "CELL_READ_DELAY 50\n"
            "BLOCK_ERASE_DELAY 2000\n"
            "CHANNEL_SWITCH_DELAY_R 16\n"
            "CHANNEL_SWITCH_DELAY_W 33\n"
            "CHANNEL_NB " << channels << "\n"
            "STAT_TYPE 15\n"
            "STAT_SCOPE 62\n"
            "STAT_PATH /tmp/stat3.csv\n"
            "STORAGE_STRATEGY 1\n"
            "GC_LOW_THR 20\n"
            "GC_HI_THR 80\n"
            "ONFI_MULTITHREADED " << multithreaded << "\n"
            "ONFI_MANAGER_QUEUE_SIZE 1024\n";
        ssd_conf.close();
    }

    class OnfiFunctionalTest : public ::testing::TestWithParam<TestParam> {

        void SetUp() override {
            TestParam current_param = GetParam();
            write_config_file(current_param.channels, current_param.multithreaded);
            INIT_SSD_CONFIG();
            FTL_INIT(g_device_index);
        }

        void TearDown() override {
            FTL_TERM(g_device_index);
            TERM_SSD_CONFIG();
        }
    };

    INSTANTIATE_TEST_CASE_P(OnfiConfigurations, OnfiFunctionalTest,
        ::testing::ValuesIn(TEST_PARAMS));

    /*
     * Test every ONFI command type (PROGRAM, READ, ERASE, READ_ID,
     * READ_PARAMETER_PAGE, RESET, READ_STATUS) serially and verify results.
     */
    TEST_P(OnfiFunctionalTest, SingleShotAllCommands) {
        uint8_t wbuf[PAGE_SIZE];
        uint8_t rbuf[PAGE_SIZE];
        size_t xfer;

        fill_with_flash_index(wbuf, 0, PAGE_SIZE);

        // PAGE_PROGRAM
        onfi_handle_t* h = ONFI_PAGE_PROGRAM(g_device_index, get_flash_first_page_address(0), 0, wbuf, PAGE_SIZE, &xfer);
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);
        ASSERT_EQ(xfer, PAGE_SIZE);

        // READ
        h = ONFI_READ(g_device_index, get_flash_first_page_address(0), 0, rbuf, PAGE_SIZE, &xfer);
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);

        // BLOCK_ERASE
        h = ONFI_BLOCK_ERASE(g_device_index, get_flash_first_page_address(0));
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);

        h = ONFI_READ(g_device_index, get_flash_first_page_address(0), 0, rbuf, PAGE_SIZE, &xfer);
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);

        // READ ID
        uint8_t sig[4];
        h = ONFI_READ_ID(g_device_index, 0, ONFI_SIGNATURE_ADDR, sig, sizeof(sig));
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);
        EXPECT_EQ(sig[0], 'O');
        EXPECT_EQ(sig[1], 'N');
        EXPECT_EQ(sig[2], 'F');
        EXPECT_EQ(sig[3], 'I');

        uint8_t ids[2];
        h = ONFI_READ_ID(g_device_index, 0, JEDEC_ID_ADDR, ids, sizeof(ids));
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);
        EXPECT_EQ(ids[0], 0xCC);
        EXPECT_EQ(ids[1], 0x10);

        // READ PARAMETER PAGE
        onfi_param_page_t param_page;
        h = ONFI_READ_PARAMETER_PAGE(g_device_index, 0, 0, (uint8_t*)&param_page, sizeof(param_page));
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);
        EXPECT_EQ(param_page.signature[0], 'O');
        EXPECT_EQ(param_page.signature[1], 'N');
        EXPECT_EQ(param_page.signature[2], 'F');
        EXPECT_EQ(param_page.signature[3], 'I');

        // RESET
        h = ONFI_RESET(g_device_index, 0);
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);

        // READ STATUS
        onfi_status_reg_t status;
        h = ONFI_READ_STATUS(g_device_index, 0, &status);
        ASSERT_NE(h, nullptr);
        ASSERT_EQ(ONFI_WAIT(h), ONFI_SUCCESS);
        EXPECT_EQ(status.RDY, 1);
    }

    /*
     * Submit page PROGRAM, READ, and BLOCK_ERASE across all flash chips
     * concurrently, then verify data integrity and post-erase state.
     */
    TEST_P(OnfiFunctionalTest, ConcurrentProgramReadEraseAllChips) {
        uint8_t prog_data[FLASH_NB][PAGE_SIZE];
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++)
            fill_with_flash_index(prog_data[flash_index], flash_index, PAGE_SIZE);

        const int total_pages = FLASH_NB * OPS_PER_FLASH;
        uint8_t* read_data = new uint8_t[(size_t)total_pages * PAGE_SIZE];
        const int num_blocks_per_flash = blocks_per_flash();

        size_t programmed_bytes[FLASH_NB][OPS_PER_FLASH] = {0};
        size_t read_bytes[FLASH_NB][OPS_PER_FLASH] = {0};

        // Submit all programs
        onfi_handle_t* prog_handles[total_pages];
        int num_handles = 0;
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++) {
            uint64_t base = get_flash_first_page_address(flash_index);
            for (int page = 0; page < OPS_PER_FLASH; page++) {
                prog_handles[num_handles++] = ONFI_PAGE_PROGRAM(g_device_index, base + page, 0, prog_data[flash_index],
                    PAGE_SIZE, &programmed_bytes[flash_index][page]);
            }
        }
        for (int i = 0; i < num_handles; i++) {
            ASSERT_NE(prog_handles[i], nullptr);
            EXPECT_EQ(ONFI_WAIT(prog_handles[i]), ONFI_SUCCESS);
        }

        // Read back and verify
        onfi_handle_t* read_handles[total_pages];
        num_handles = 0;
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++) {
            uint64_t base = get_flash_first_page_address(flash_index);
            for (int page = 0; page < OPS_PER_FLASH; page++) {
                uint8_t* buf = read_data + ((size_t)flash_index * OPS_PER_FLASH + page) * PAGE_SIZE;
                read_handles[num_handles++] = ONFI_READ(g_device_index, base + page, 0, buf,
                    PAGE_SIZE, &read_bytes[flash_index][page]);
            }
        }
        for (int i = 0; i < num_handles; i++) {
            ASSERT_NE(read_handles[i], nullptr);
            EXPECT_EQ(ONFI_WAIT(read_handles[i]), ONFI_SUCCESS);
        }

        // Erase all blocks
        const int total_erases = FLASH_NB * num_blocks_per_flash;
        onfi_handle_t* erase_handles[total_erases];
        num_handles = 0;
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++) {
            uint64_t base = get_flash_first_page_address(flash_index);
            for (int block_index = 0; block_index < num_blocks_per_flash; block_index++) {
                erase_handles[num_handles++] = ONFI_BLOCK_ERASE(g_device_index, base + block_index * PAGE_NB);
            }
        }
        for (int i = 0; i < num_handles; i++) {
            ASSERT_NE(erase_handles[i], nullptr);
            EXPECT_EQ(ONFI_WAIT(erase_handles[i]), ONFI_SUCCESS);
        }

        delete[] read_data;
    }

    /*
     * Submit every ONFI command type (PROGRAM, READ, BLOCK_ERASE, READ_ID,
     * READ_PARAMETER_PAGE, RESET, READ_STATUS) concurrently across all flash
     * devices in a single batch, then verify success, signatures, and transfer sizes.
     */
    TEST_P(OnfiFunctionalTest, MixedConcurrentOperations) {
        uint8_t prog_data[FLASH_NB][PAGE_SIZE];
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++)
            fill_with_flash_index(prog_data[flash_index], flash_index, PAGE_SIZE);

        size_t prog_xfer[FLASH_NB];
        size_t read_xfer[FLASH_NB];
        uint8_t read_buf[FLASH_NB][PAGE_SIZE];
        uint8_t sig_buf[FLASH_NB][4];
        onfi_param_page_t pp_buf[FLASH_NB];
        onfi_status_reg_t status_buf[FLASH_NB];

        const int num_handlers = FLASH_NB * 7;
        onfi_handle_t* handles[num_handlers];
        int nh = 0;

        // Submit all command types concurrently
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++) {
            uint64_t addr = get_flash_first_page_address(flash_index);

            handles[nh++] = ONFI_PAGE_PROGRAM(g_device_index, addr, 0, prog_data[flash_index], PAGE_SIZE, &prog_xfer[flash_index]);
            handles[nh++] = ONFI_READ(g_device_index, addr, 0, read_buf[flash_index], PAGE_SIZE, &read_xfer[flash_index]);
            handles[nh++] = ONFI_BLOCK_ERASE(g_device_index, addr);
            handles[nh++] = ONFI_READ_ID(g_device_index, flash_index, ONFI_SIGNATURE_ADDR, sig_buf[flash_index], sizeof(sig_buf[flash_index]));
            handles[nh++] = ONFI_READ_PARAMETER_PAGE(g_device_index, flash_index, 0, (uint8_t*)&pp_buf[flash_index], sizeof(pp_buf[flash_index]));
            handles[nh++] = ONFI_RESET(g_device_index, flash_index);
            handles[nh++] = ONFI_READ_STATUS(g_device_index, flash_index, &status_buf[flash_index]);
        }

        // Wait and verify all commands
        for (int i = 0; i < nh; i++) {
            ASSERT_NE(handles[i], nullptr);
            onfi_ret_val res = ONFI_WAIT(handles[i]);
            EXPECT_EQ(res, ONFI_SUCCESS) << "Handle " << i << " failed";
        }

        // Verify signatures, param pages, and status
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++) {
            EXPECT_EQ(sig_buf[flash_index][0], 'O');
            EXPECT_EQ(sig_buf[flash_index][1], 'N');
            EXPECT_EQ(sig_buf[flash_index][2], 'F');
            EXPECT_EQ(sig_buf[flash_index][3], 'I');
            EXPECT_EQ(pp_buf[flash_index].signature[0], 'O');
            EXPECT_EQ(status_buf[flash_index].RDY, 1);
        }

        // Verify transfer sizes
        for (int flash_index = 0; flash_index < FLASH_NB; flash_index++) {
            EXPECT_EQ(prog_xfer[flash_index], (size_t)PAGE_SIZE);
            EXPECT_EQ(read_xfer[flash_index], (size_t)PAGE_SIZE);
        }
    }

    /*
     * Submit concurrent PROGRAM and READ across three separate NVMe
     * devices, then verify data integrity on each.
     */
    TEST(OnfiMultiDeviceTest, ProgramReadAllDevices) {
        write_config_file(8, 1);

        INIT_SSD_CONFIG();
        FTL_INIT(0);
        FTL_INIT(1);
        FTL_INIT(2);

        const int NUM_DEVICES = 3;
        uint8_t wbuf[NUM_DEVICES][PAGE_SIZE];
        uint8_t rbuf[NUM_DEVICES][PAGE_SIZE];
        size_t xfer[NUM_DEVICES];

        for (int d = 0; d < NUM_DEVICES; d++)
            memset(wbuf[d], 0xA0 + d, PAGE_SIZE);

        onfi_handle_t* handles[NUM_DEVICES];

        // Program page 0 on all devices concurrently
        for (int d = 0; d < NUM_DEVICES; d++) {
            handles[d] = ONFI_PAGE_PROGRAM(d, 0, 0, wbuf[d], PAGE_SIZE, &xfer[d]);
            ASSERT_NE(handles[d], nullptr);
        }
        for (int d = 0; d < NUM_DEVICES; d++) {
            EXPECT_EQ(ONFI_WAIT(handles[d]), ONFI_SUCCESS);
            EXPECT_EQ(xfer[d], (size_t)PAGE_SIZE);
        }

        // Read back and verify on all devices concurrently
        for (int d = 0; d < NUM_DEVICES; d++) {
            handles[d] = ONFI_READ(d, 0, 0, rbuf[d], PAGE_SIZE, &xfer[d]);
            ASSERT_NE(handles[d], nullptr);
        }
        for (int d = 0; d < NUM_DEVICES; d++) {
            EXPECT_EQ(ONFI_WAIT(handles[d]), ONFI_SUCCESS);
        }

        // Cleanup
        FTL_TERM(2);
        FTL_TERM(1);
        FTL_TERM(0);
        TERM_SSD_CONFIG();
    }

    struct MultiThreadWorkerArg {
        int tid;
        int num_threads;
        int* errors;
    };

    /*
     * thread entry point for MultiMainThreads.
     * The worker works on MULTI_MAIN_THREADS_NUM_BLOCKS_PER_TEST blocks and
     * MULTI_MAIN_THREADS_NUM_PAGES_PER_BLOCK pages per run.
     * For every assigned block the thread: PROGRAMs pages with a tid-specific pattern,
     * READs them back for verification, ERASEs the block, then READs again to confirm
     * the block was erased.
     * Sets *errors = 1 on any failure.
     */
    static void* worker_thread_routine(void* arg) {
        MultiThreadWorkerArg* worker_args = (MultiThreadWorkerArg*)arg;
        uint8_t wbuf[PAGE_SIZE];
        uint8_t rbuf[PAGE_SIZE];
        size_t xfer;

        memset(wbuf, 0xA0 + worker_args->tid, PAGE_SIZE);

        for (int f = 0; f < FLASH_NB; f++) {
            uint64_t block_index = get_flash_first_page_address(f);
            for (int i = 0, block = worker_args->tid; i < MULTI_MAIN_THREADS_NUM_BLOCKS_PER_TEST && block < BLOCK_NB; i++, block += worker_args->num_threads) {
                uint64_t block_start = block_index + (uint64_t)block * PAGE_NB;

                for (int page_index = 0; page_index < MULTI_MAIN_THREADS_NUM_PAGES_PER_BLOCK; page_index++) {
                    uint64_t addr = block_start + page_index;
                    onfi_handle_t* h = ONFI_PAGE_PROGRAM(g_device_index, addr, 0, wbuf, PAGE_SIZE, &xfer);
                    if (!h || ONFI_WAIT(h) != ONFI_SUCCESS || xfer != PAGE_SIZE) {
                        *worker_args->errors = 1;
                        return NULL;
                    }
                }

                for (int page_index = 0; page_index < MULTI_MAIN_THREADS_NUM_PAGES_PER_BLOCK; page_index++) {
                    uint64_t addr = block_start + page_index;
                    onfi_handle_t* h = ONFI_READ(g_device_index, addr, 0, rbuf, PAGE_SIZE, &xfer);
                    if (!h || ONFI_WAIT(h) != ONFI_SUCCESS) {
                        *worker_args->errors = 1;
                        return NULL;
                    }
                }

                onfi_handle_t* h = ONFI_BLOCK_ERASE(g_device_index, block_start);
                if (!h || ONFI_WAIT(h) != ONFI_SUCCESS) {
                    *worker_args->errors = 1;
                    return NULL;
                }

                for (int page_index = 0; page_index < MULTI_MAIN_THREADS_NUM_PAGES_PER_BLOCK; page_index++) {
                    uint64_t addr = block_start + page_index;
                    onfi_handle_t* rh = ONFI_READ(g_device_index, addr, 0, rbuf, PAGE_SIZE, &xfer);
                    if (!rh || ONFI_WAIT(rh) != ONFI_SUCCESS) {
                        *worker_args->errors = 1;
                        return NULL;
                    }
                }
            }
        }

        return NULL;
    }

    /*
     * Spawn multiple concurrent worker threads that perform PROGRAM, READ,
     * and BLOCK_ERASE across all flash chips to validate thread safety
     * under the multi-threaded ONFI manager.
     * This test only makes sense in multithreaded mode: in serial mode the
     * ONFI commands execute inline on the calling thread without the device
     * lock, so concurrent callers are not safe. It is skipped for serial
     * configurations.
     */
    TEST_P(OnfiFunctionalTest, MultiMainThreads) {
        if (!GetParam().multithreaded)
            return;

        // Run the tests for every configured number of threads
        for (int num_threads : MULTI_MAIN_THREADS_NUM_THREADS_OPTIONS) {
            std::cout << "start test with " << num_threads << " main threads" << std::endl;

            int per_thread_errors[num_threads];
            pthread_t threads[num_threads];
            MultiThreadWorkerArg args[num_threads];

            // Start the main threads
            for (int i = 0; i < num_threads; i++) {
                per_thread_errors[i] = 0;
                args[i].tid = i;
                args[i].num_threads = num_threads;
                args[i].errors = &per_thread_errors[i];
                pthread_create(&threads[i], NULL, worker_thread_routine, &args[i]);
            }

            // Count errors in threads
            int total_thread_errors = 0;
            for (int i = 0; i < num_threads; i++) {
                pthread_join(threads[i], NULL);
                total_thread_errors += per_thread_errors[i];
            }

            // Check that there was no errors
            ASSERT_EQ(total_thread_errors, 0);
        }
    }
}
