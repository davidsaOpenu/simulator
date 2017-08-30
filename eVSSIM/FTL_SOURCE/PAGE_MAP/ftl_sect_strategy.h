#ifndef _FTL_SECT_H_
#define _FTL_SECT_H_

#include "ftl.h"

ftl_ret_val _FTL_READ_SECT(uint64_t sector_nb, unsigned int length);
ftl_ret_val _FTL_WRITE_SECT(uint64_t sector_nb, unsigned int length);
void _FTL_SECT_STRATEGY_INIT(partition_id_t part_id, psize_t size);
void _FTL_SECT_STRATEGY_TERM(partition_id_t part_id);

ftl_ret_val _FTL_READ(uint64_t sector_nb, unsigned int offset, unsigned int length);
ftl_ret_val _FTL_WRITE(uint64_t sector_nb, unsigned int offset, unsigned int length);
ftl_ret_val _FTL_COPYBACK(int32_t source, int32_t destination);
ftl_ret_val _FTL_CREATE(size_t size);
ftl_ret_val _FTL_DELETE(uint64_t id);

#endif
