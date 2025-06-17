#ifndef _FTL_SECT_H_
#define _FTL_SECT_H_

#include "ftl.h"

ftl_ret_val _FTL_READ_SECT(uint32_t  nsid, uint64_t sector_nb, unsigned int length);
ftl_ret_val _FTL_WRITE_SECT(uint32_t nsid, uint64_t sector_nb, unsigned int length);

ftl_ret_val _FTL_READ(uint32_t nsid, uint64_t sector_nb, unsigned int offset, unsigned int length);
ftl_ret_val _FTL_WRITE(uint32_t nsid, uint64_t sector_nb, unsigned int length);
ftl_ret_val _FTL_COPYBACK(uint64_t source, uint64_t destination);
ftl_ret_val _FTL_CREATE(void);
ftl_ret_val _FTL_DELETE(void);

#endif
