#ifndef SSD_FILE_OPS_H
#define SSD_FILE_OPS_H

// This library simulates SSD on a given file

#include <stddef.h>
#include <stdbool.h>

typedef enum {SSD_FILE_OPS_ERROR, SSD_FILE_OPS_SUCCESS} ssd_file_ops_ret_val;

// Creates a file that simulated a SSD with a fiven size.
// The function fills the SSD file with 1s like a real SSD that all its memory is erased.
ssd_file_ops_ret_val ssd_create(const char *path, size_t capacity_size);

// Checks if writing `buff` to the requested place will only switch 1 to 0 (so ssd don't need to be erased for writing).
bool is_program_compatible(const char *path, size_t offset, size_t length, const unsigned char *buff);

// Writes a given buffer to the SSD file (there is no check on the buffer content).
ssd_file_ops_ret_val ssd_write(const char *path, size_t offset, size_t length, const unsigned char *buff);

// Reads SSD file content to a given buffer.
ssd_file_ops_ret_val ssd_read(const char *path, size_t offset, size_t length, unsigned char *buff);

#endif