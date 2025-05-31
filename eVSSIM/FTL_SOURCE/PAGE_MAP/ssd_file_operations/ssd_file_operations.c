#include "ssd_file_operations.h"
#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

#define BUFFER_SIZE (4096)

size_t ssd_get_capacity(const char *path) {
    if (path == NULL) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_size;
}

ssd_file_ops_ret_val ssd_create(const char *path, size_t capacity_size) {
    if (path == NULL) return SSD_FILE_OPS_ERROR;
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) return SSD_FILE_OPS_ERROR;

    unsigned char buffer[BUFFER_SIZE];

    // Fill with 1s like a real ssd
    memset(buffer, 0xFF, sizeof(buffer));

    size_t written = 0;
    while (written < capacity_size) {
        size_t to_write = (capacity_size - written < sizeof(buffer)) ? (capacity_size - written) : sizeof(buffer);
        ssize_t w = write(fd, buffer, to_write);
        if (w <= 0) {
            close(fd);
            return SSD_FILE_OPS_ERROR;
        }
        written += w;
    }

    close(fd);
    return SSD_FILE_OPS_SUCCESS;
}

bool is_program_compatible(const char *path, size_t offset, size_t length, const unsigned char *buff) {
    if (path == NULL || buff == NULL) return SSD_FILE_OPS_ERROR;
    const unsigned char* new_data = (const unsigned char*)buff;
    unsigned char existing_data[BUFFER_SIZE];

    size_t i = 0;

    size_t processed = 0;
    while (processed < length) {
        size_t to_check = (length - processed < BUFFER_SIZE) ? (length - processed) : BUFFER_SIZE;

        if (ssd_read(path, offset + processed, to_check, existing_data) != SSD_FILE_OPS_SUCCESS) return false;

        for (i = 0; i < to_check; i++) {
            if ((existing_data[i] & new_data[processed + i]) != new_data[processed + i]) {
                return false;
            }
        }
        processed += to_check;
    }
    return true;

}

ssd_file_ops_ret_val ssd_write(const char *path, size_t offset, size_t length, const unsigned char *buff) {
    if (path == NULL || buff == NULL) return SSD_FILE_OPS_ERROR;
    size_t capacity = ssd_get_capacity(path);
    if (capacity == 0) return SSD_FILE_OPS_ERROR;

    if (offset + length > capacity) return SSD_FILE_OPS_ERROR;

    int fd = open(path, O_WRONLY);
    if (fd < 0) return SSD_FILE_OPS_ERROR;

    ssize_t written = pwrite(fd, buff, length, offset);
    close(fd);

    return (written == (ssize_t)length) ? SSD_FILE_OPS_SUCCESS : SSD_FILE_OPS_ERROR;
}

ssd_file_ops_ret_val ssd_read(const char *path, size_t offset, size_t length, unsigned char *buff) {
    if (path == NULL || buff == NULL) return SSD_FILE_OPS_ERROR;
    size_t capacity = ssd_get_capacity(path);
    if (capacity == 0) return SSD_FILE_OPS_ERROR;

    if (offset + length > capacity) return SSD_FILE_OPS_ERROR;


    int fd = open(path, O_RDONLY);
    if (fd < 0) return SSD_FILE_OPS_ERROR;

    ssize_t read_bytes = pread(fd, buff, length, offset);
    close(fd);

    return (read_bytes == (ssize_t)length) ? SSD_FILE_OPS_SUCCESS : SSD_FILE_OPS_ERROR;
}

