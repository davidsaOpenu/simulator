#ifndef _FTL_ADR_H_
#define _FTL_ADR_H_

#include "common.h"

int _FTL_READ_ADR(int32_t sector_nb, unsigned int length);
int _FTL_WRITE_ADR(int32_t sector_nb, unsigned int length);

int _FTL_READ(int32_t sector_nb, unsigned int offset, unsigned int length);
int _FTL_WRITE(int32_t sector_nb, unsigned int offset, unsigned int length);
int _FTL_COPYBACK(int32_t source, int32_t destination);
int _FTL_CREATE(size_t size);
int _FTL_DELETE(int32_t id);

#endif
