#ifndef _FTL_OBJ_H_
#define _FTL_OBJ_H_

#include <stdlib.h>
#include "ftl.h"
#include "uthash.h"

typedef uint64_t object_id_t;
typedef uint64_t partition_id_t;

typedef unsigned int length_t;
typedef unsigned int offset_t;
typedef uint8_t *buf_ptr_t;

/* unique object locator */
typedef struct obj_id {
    object_id_t object_id;
    uint64_t partition_id;
} obj_id_t;

/* A page node in the linked list of object-mapped pages */
typedef struct page_node {
    uint32_t page_id;
    object_id_t object_id;
    struct page_node *next;
    UT_hash_handle hh; /* makes this structure hashable */
} page_node;

/* The object struct. Metadata will be added as a pointer to another struct or as more fields */
typedef struct stored_object {
    object_id_t id;
    size_t size;
    page_node *pages;
    UT_hash_handle hh; /* makes this structure hashable */
} stored_object;

/* struct which will hold the ids of existing objects */
typedef struct object_map {
    object_id_t id;
    bool exists;
    UT_hash_handle hh; /* makes this structure hashable */
} object_map;

/* struct which will hold the ids of existing partitions */
typedef struct partition_map {
	partition_id_t id;
    bool exists;
    UT_hash_handle hh; /* makes this structure hashable */
} partition_map;

/*
 * TODO: this struct may be redundant with obj_id_t
 */
typedef struct {
	uint64_t partition_id;
	uint64_t object_id;
	bool create_object;
} object_location;

typedef struct {
	 uint8_t *metadata_mapping_addr;
	 unsigned int metadata_size;
} metadata_info;

void INIT_OBJ_STRATEGY(void);
void TERM_OBJ_STRATEGY(void);

/* FTL functions */
ftl_ret_val _FTL_OBJ_READ(obj_id_t object_loc, offset_t offset,length_t length);
ftl_ret_val _FTL_OBJ_WRITE(obj_id_t object_loc, buf_ptr_t buf, offset_t offset, length_t length);
ftl_ret_val _FTL_OBJ_COPYBACK(int32_t source, int32_t destination);
bool _FTL_OBJ_CREATE(obj_id_t obj_loc, size_t size);
ftl_ret_val _FTL_OBJ_DELETE(obj_id_t object_loc);


/* Persistent OSD storge */
bool osd_init(void);
bool create_partition(partition_id_t part_id);
void OSD_WRITE_OBJ(object_location obj_loc, unsigned int length, uint8_t *buf);
void OSD_READ_OBJ(object_location obj_loc, unsigned int length, uint64_t addr, uint64_t offset);

/* Helper functions */
stored_object *lookup_object(object_id_t object_id);
object_map *lookup_object_mapping(object_id_t object_id);
stored_object *create_object(object_id_t obj_id, size_t size);
int remove_object(stored_object *object, object_map *obj_map);

page_node *allocate_new_page(object_id_t object_id, uint32_t page_id);
page_node *add_page(stored_object *object, uint32_t page_id);
page_node *page_by_offset(stored_object *object, unsigned int offset);
page_node *lookup_page(uint32_t page_id);
page_node *next_page(stored_object *object,page_node *current);
void free_obj_table(void);
void free_page_table(void);
void free_obj_mapping(void);
void free_part_mapping(void);

void printMemoryDump(uint8_t *buffer, unsigned int bufferLength);

#endif
