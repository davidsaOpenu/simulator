/*
 * QEMU SSD Emulator
 *
 * Copyright (c) 2009 Kim, Joo Hyun
 * Copyright (c) 2009 Hanyang Univ. 
 * Copyright (c) 2009 Samsung Electronics. 
 *
 */

#ifndef _SSD_H_
#define _SSD_H_

#include "hw.h"


//FILE *fp;
void SSD_INIT(void);
void SSD_TERM(void);

void SSD_WRITE(unsigned int length, int32_t sector_nb);
void SSD_READ(unsigned int length, int32_t sector_nb);

#endif
