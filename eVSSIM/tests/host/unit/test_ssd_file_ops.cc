
extern "C" {
    #include "ssd_file_operations.h"
};

#include <gtest/gtest.h>
#include <string.h>
#include <stdio.h>

#define TEST_FILE "test_flash.img"
#define SECTOR_SIZE 512

using namespace std;

namespace ssd_file_ops_test
{
    class SsdFileOpsTest : public ::testing::Test
    {
    public:
        virtual void SetUp()
        {
            // Every test start with new ssd image file.
            ASSERT_EQ(SSD_FILE_OPS_SUCCESS, ssd_create(TEST_FILE, 4096));
        }

        virtual void TearDown()
        {
            remove(TEST_FILE);
        }
    };

    TEST_F(SsdFileOpsTest, WriteAndReadSuccessTest) {
        unsigned char data[SECTOR_SIZE] = "SSD sector test";
        unsigned char read_back[SECTOR_SIZE] = {0};

        ASSERT_EQ(SSD_FILE_OPS_SUCCESS, ssd_write(TEST_FILE, 0, SECTOR_SIZE, data));
        ASSERT_EQ(SSD_FILE_OPS_SUCCESS, ssd_read(TEST_FILE, 0, SECTOR_SIZE, read_back));
        ASSERT_EQ(memcmp(data, read_back, SECTOR_SIZE), 0); 
    }

    TEST_F(SsdFileOpsTest, IsProgramCompatibleTrueTest) {
        unsigned char data[SECTOR_SIZE];
        memset(data, 0xF0, SECTOR_SIZE); // 1s flipped to 0s — should be allowed
        ASSERT_TRUE(is_program_compatible(TEST_FILE, 0, SECTOR_SIZE, data));
    }

    TEST_F(SsdFileOpsTest, IsProgramCompatibleFalseTest) {
        unsigned char zero_data[SECTOR_SIZE];
        memset(zero_data, 0x00, SECTOR_SIZE);
        ASSERT_EQ(SSD_FILE_OPS_SUCCESS, ssd_write(TEST_FILE, 0, SECTOR_SIZE, zero_data));

        unsigned char attempt_to_flip[SECTOR_SIZE];
        memset(attempt_to_flip, 0xFF, SECTOR_SIZE); // attempt to go from 0 to 1 — illegal
        ASSERT_FALSE(is_program_compatible(TEST_FILE, 0, SECTOR_SIZE, attempt_to_flip));
    }

    TEST_F(SsdFileOpsTest, WriteOutOfBoundsShouldFailTest) {
        unsigned char data[SECTOR_SIZE] = "Fail write";
        ASSERT_EQ(SSD_FILE_OPS_ERROR, ssd_write(TEST_FILE, 4096, SECTOR_SIZE, data));
    }

    TEST_F(SsdFileOpsTest, ReadOutOfBoundsShouldFailTest) {
        unsigned char buf[SECTOR_SIZE];
        ASSERT_EQ(SSD_FILE_OPS_ERROR, ssd_read(TEST_FILE, 5000, SECTOR_SIZE, buf));
    }

    TEST_F(SsdFileOpsTest, ReadGetNullBuffTest) {
        ASSERT_EQ(SSD_FILE_OPS_ERROR, ssd_read(TEST_FILE, 5000, SECTOR_SIZE, NULL));
    }

    TEST_F(SsdFileOpsTest, WriteGetNullBuffTest) {
        ASSERT_EQ(SSD_FILE_OPS_ERROR, ssd_write(TEST_FILE, 5000, SECTOR_SIZE, NULL));
    }

    TEST_F(SsdFileOpsTest, CreateGetNullPathTest) {
        ASSERT_EQ(SSD_FILE_OPS_ERROR, ssd_create(NULL, 5000));
    }

    TEST_F(SsdFileOpsTest, WriteGetNullPathTest) {
        unsigned char data[SECTOR_SIZE] = "Fail write";
        ASSERT_EQ(SSD_FILE_OPS_ERROR, ssd_write(NULL, 4096, SECTOR_SIZE, data));
    }

    TEST_F(SsdFileOpsTest, ReadGetNullPathTest) {
        unsigned char data[SECTOR_SIZE];
        ASSERT_EQ(SSD_FILE_OPS_ERROR, ssd_read(NULL, 5000, SECTOR_SIZE, data));
    }
};
