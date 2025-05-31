#ifndef _FTL_SECT_H_
#define _FTL_SECT_H_

#include "ftl.h"

// NOTE: `data` buffer should be the size of `length` * SECTOR_SIZE because `length` means amount of sectors.
ftl_ret_val _FTL_READ_SECT(uint64_t sector_nb, unsigned int length);
ftl_ret_val _FTL_WRITE_SECT(uint64_t sector_nb, unsigned int length, const unsigned char *data);
ftl_ret_val _FTL_READ(uint64_t sector_nb, unsigned int length, unsigned char *data);
ftl_ret_val _FTL_WRITE(uint64_t sector_nb, unsigned int length, const unsigned char *data);

ftl_ret_val _FTL_COPYBACK(uint64_t source, uint64_t destination);
ftl_ret_val _FTL_CREATE(void);
ftl_ret_val _FTL_DELETE(void);

#endif
