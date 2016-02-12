// Copyright(c)2013 
//
// Hanyang University, Seoul, Korea
// Embedded Software Systems Lab. All right reserved 

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdbool.h>
#include "ftl_type.h"
#include <inttypes.h>

/* FTL */
#define FTL_DEBUG 1

/* VSSIM Function */
#define MONITOR_ON
#ifdef PAGE_MAP
#define GC_ON 1			/* Garbage Collection for PAGE MAP */
#endif
#define DEL_QEMU_OVERHEAD
//#define O_DIRECT_VSSIM			/* O_DIRECT IO mode */

#include "vssim_config_manager.h"
#include "ftl.h"
#include "ftl_inverse_mapping_manager.h"
#include "ftl_perf_manager.h"

#include "ssd_io_manager.h"
#include "ssd_log_manager.h"

#ifdef PAGE_MAP
	#include "ftl_gc_manager.h"
	#include "ftl_mapping_manager.h"
#endif

#define SUCCESSFUL		1
#define FAILURE		0

/* Block Type */
#define EMPTY_BLOCK             30
#define SEQ_BLOCK               31
#define EMPTY_SEQ_BLOCK         32
#define RAN_BLOCK               33
#define EMPTY_RAN_BLOCK         34
#define RAN_COLD_BLOCK		35
#define EMPTY_RAN_COLD_BLOCK	36
#define	RAN_HOT_BLOCK		37
#define	EMPTY_RAN_HOT_BLOCK	38
#define DATA_BLOCK              39
#define EMPTY_DATA_BLOCK        40

/* GC Copy Valid Page Type */
#define VICTIM_OVERALL	41
#define VICTIM_INCHIP	42
#define VICTIM_NOPARAL	43

/* Page Type */
#define VALID		50
#define INVALID		51

/* Perf Checker Calloc Type */
#define CH_OP		80
#define REG_OP		81
#define LATENCY_OP	82

#define CHANNEL_IS_EMPTY 700
#define CHANNEL_IS_WRITE 701
#define CHANNEL_IS_READ  702
#define CHANNEL_IS_ERASE 703

#define REG_IS_EMPTY 		705
#define REG_IS_WRITE 		706
#define REG_IS_READ  		707
#define REG_IS_ERASE 		708

#define NOOP			800
#define READ  			801
#define WRITE			802
#define ERASE 			803
#define GC_READ			804
#define GC_WRITE		805
#define SEQ_WRITE		806
#define RAN_WRITE		807
#define RAN_COLD_WRITE		808
#define RAN_HOT_WRITE		809
#define SEQ_MERGE_READ		810
#define RAN_MERGE_READ		811
#define SEQ_MERGE_WRITE		812
#define RAN_MERGE_WRITE		813
#define RAN_COLD_MERGE_READ	814
#define RAN_HOT_MERGE_READ	815
#define RAN_COLD_MERGE_WRITE	816
#define RAN_HOT_MERGE_WRITE	817
#define MAP_READ		818
#define MAP_WRITE		819
#define COPYBACK		820

#define UPDATE_START_TIME	900
#define UPDATE_END_TIME		901
#define UPDATE_GC_START_TIME	902
#define UPDATE_GC_END_TIME	903

#define SUPPORTED_OPERATION 4
#define LOGICAL_READ	0
#define	LOGICAL_WRITE	1
#define PHYSICAL_READ	2
#define PHYSICAL_WRITE	3

#define COLLECT_LOGICAL_READ	1
#define COLLECT_LOGICAL_WRITE	2
#define COLLECT_PHYSICAL_READ	4
#define COLLECT_PHYSICAL_WRITE	8

#define PAGE			1100
#define BLOCK 			1101
#define PLANE			1102
#define FLASH			1103
#define CHANNEL			1104
#define SSD				1105

#define COLLECT_PAGE	1
#define	COLLECT_BLOCK	2
#define COLLECT_PLANE	4
#define COLLECT_FLASH	8
#define COLLECT_CHANNEL	16
#define COLLECT_SSD		32

/* VSSIM Function Debug */
#define MNT_DEBUG			// MONITOR Debugging

/* FTL Debugging */
//#define FTL_DEBUG

/* SSD Debugging */
//#define SSD_DEBUG

#endif
