#ifndef _FTL_SECT_H_
#define _FTL_SECT_H_

#include "ftl.h"
#include <stddef.h>
#include <stdbool.h>

// Sector write strategy API functions to be called by QEMU
ftl_ret_val FTL_READ_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data);
ftl_ret_val FTL_WRITE_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data);

// NOTE: `data` buffer should be the size of `length` * SECTOR_SIZE because `length` means amount of sectors.
ftl_ret_val _FTL_READ_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data);
ftl_ret_val _FTL_WRITE_SECT(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data);
ftl_ret_val _FTL_READ(uint8_t device_index, uint64_t sector_nb, unsigned int length, unsigned char *data);
ftl_ret_val _FTL_WRITE(uint8_t device_index, uint64_t sector_nb, unsigned int length, const unsigned char *data);

ftl_ret_val _FTL_COPYBACK(uint8_t device_index, uint64_t source, uint64_t destination);
ftl_ret_val _FTL_CREATE(uint8_t device_index);
ftl_ret_val _FTL_DELETE(void);

#endif
