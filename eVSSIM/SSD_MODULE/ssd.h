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

#include <stdint.h>
#include "ftl_obj_strategy.h"

void SSD_INIT(void);
void SSD_TERM(void);

void SSD_WRITE(unsigned int length, uint32_t sector_nb);
void SSD_READ(unsigned int length, uint32_t sector_nb);
int SSD_CREATE(unsigned int length);

void SSD_OBJECT_READ(object_location obj_loc, unsigned int length);
void SSD_OBJECT_WRITE(object_location obj_loc, unsigned int length);

void OSD_WRITE(object_location obj_loc, unsigned int length, uint64_t addr);
void OSD_READ(object_location obj_loc, unsigned int length, uint64_t addr);

#endif
