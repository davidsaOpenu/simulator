#ifndef _FTL_SECT_H_
#define _FTL_SECT_H_

#include "ftl.h"
#include <stddef.h>
#include <stdbool.h>

// NOTE: `data` buffer should be the size of `length` * SECTOR_SIZE because `length` means amount of sectors.
ftl_ret_val _FTL_READ_SECT(uint64_t sector_nb, unsigned int length, unsigned char *data);
ftl_ret_val _FTL_WRITE_SECT(uint64_t sector_nb, unsigned int length, const unsigned char *data);
ftl_ret_val _FTL_READ(uint64_t sector_nb, unsigned int length, unsigned char *data);
ftl_ret_val _FTL_WRITE(uint64_t sector_nb, unsigned int length, const unsigned char *data);

ftl_ret_val _FTL_COPYBACK(uint64_t source, uint64_t destination);
ftl_ret_val _FTL_CREATE(void);
ftl_ret_val _FTL_DELETE(void);


// ******************************************************************
// * Helper functions that help in simulating SSD on a given file. *
// ******************************************************************

typedef enum {SSD_FILE_OPS_ERROR, SSD_FILE_OPS_SUCCESS} ssd_file_ops_ret_val;

// Creates a file that simulated a SSD with a fiven capacity size.
// The function fills the SSD file with 1s like a real SSD that all its memory is erased.
ssd_file_ops_ret_val ssd_create(const char *path, size_t capacity_size);

// Checks if writing `buff` to the requested place will only switch 1 to 0 (so ssd don't need to be erased for writing).
bool is_program_compatible(const char *path, size_t offset, size_t length, const unsigned char *buff);

// Writes a given buffer to the SSD file (there is no check on the buffer content).
ssd_file_ops_ret_val ssd_write(const char *path, size_t offset, size_t length, const unsigned char *buff);

// Reads SSD file content to a given buffer.
ssd_file_ops_ret_val ssd_read(const char *path, size_t offset, size_t length, unsigned char *buff);

#endif
