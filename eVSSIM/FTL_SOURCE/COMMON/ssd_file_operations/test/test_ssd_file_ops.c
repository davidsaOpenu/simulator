#include "../ssd_file_operations.h"
#include "unity.h"
#include <string.h>
#include <stdio.h>

#define TEST_FILE "test_flash.img"
#define SECTOR_SIZE 512

void setUp(void) {
    // Every test start with new ssd image file.
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_SUCCESS, ssd_create(TEST_FILE, 4096));
}

void tearDown(void) {
    remove(TEST_FILE);
}

void test_write_and_read_success(void) {
    unsigned char data[SECTOR_SIZE] = "SSD sector test";
    unsigned char read_back[SECTOR_SIZE] = {0};

    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_SUCCESS, ssd_write(TEST_FILE, 0, SECTOR_SIZE, data));
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_SUCCESS, ssd_read(TEST_FILE, 0, SECTOR_SIZE, read_back));
    TEST_ASSERT_EQUAL_MEMORY(data, read_back, SECTOR_SIZE);
}

void test_is_program_compatible_true(void) {
    unsigned char data[SECTOR_SIZE];
    memset(data, 0xF0, SECTOR_SIZE); // 1s flipped to 0s — should be allowed
    TEST_ASSERT_TRUE(is_program_compatible(TEST_FILE, 0, SECTOR_SIZE, data));
}

void test_is_program_compatible_false(void) {
    unsigned char zero_data[SECTOR_SIZE];
    memset(zero_data, 0x00, SECTOR_SIZE);
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_SUCCESS, ssd_write(TEST_FILE, 0, SECTOR_SIZE, zero_data));

    unsigned char attempt_to_flip[SECTOR_SIZE];
    memset(attempt_to_flip, 0xFF, SECTOR_SIZE); // attempt to go from 0 to 1 — illegal
    TEST_ASSERT_FALSE(is_program_compatible(TEST_FILE, 0, SECTOR_SIZE, attempt_to_flip));
}

void test_write_out_of_bounds_should_fail(void) {
    unsigned char data[SECTOR_SIZE] = "Fail write";
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_ERROR, ssd_write(TEST_FILE, 4096, SECTOR_SIZE, data));
}

void test_read_out_of_bounds_should_fail(void) {
    unsigned char buf[SECTOR_SIZE];
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_ERROR, ssd_read(TEST_FILE, 5000, SECTOR_SIZE, buf));
}

void test_read_get_null_buff(void) {
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_ERROR, ssd_read(TEST_FILE, 5000, SECTOR_SIZE, NULL));
}

void test_write_get_null_buff(void) {
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_ERROR, ssd_write(TEST_FILE, 5000, SECTOR_SIZE, NULL));
}

void test_create_get_null_path(void) {
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_ERROR, ssd_create(NULL, 5000));
}

void test_write_get_null_path(void) {
    unsigned char data[SECTOR_SIZE] = "Fail write";
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_ERROR, ssd_write(NULL, 4096, SECTOR_SIZE, data));
}

void test_read_get_null_path(void) {
    unsigned char data[SECTOR_SIZE];
    TEST_ASSERT_EQUAL_INT(SSD_FILE_OPS_ERROR, ssd_read(NULL, 5000, SECTOR_SIZE, data));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_write_and_read_success);
    RUN_TEST(test_is_program_compatible_true);
    RUN_TEST(test_is_program_compatible_false);
    RUN_TEST(test_write_out_of_bounds_should_fail);
    RUN_TEST(test_read_out_of_bounds_should_fail);
    RUN_TEST(test_read_get_null_buff);
    RUN_TEST(test_write_get_null_buff);
    RUN_TEST(test_create_get_null_path);
    RUN_TEST(test_read_get_null_path);
    RUN_TEST(test_write_get_null_path);

    return UNITY_END();
}
