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

void SSD_WRITE(int32_t id, unsigned int offset, unsigned int length);
void SSD_READ(int32_t id, unsigned int offset, unsigned int length);

#endif
