#ifndef _FTL_OBJ_H_
#define _FTL_OBJ_H_

#include <stdlib.h>
#include "ftl.h"
#include "uthash.h"
#include "osc-osd/osd-target/osd.h"
#include "osc-osd/osd-util/osd-util.h"
#include "osc-osd/osd-util/osd-defs.h"
#include "common.h"

typedef uint64_t object_id_t;
typedef uint64_t partition_id_t;

typedef unsigned int length_t;
typedef unsigned int offset_t;
typedef uint8_t *buf_ptr_t;

/* unique object locator */
typedef struct obj_id
{
    object_id_t object_id;
    partition_id_t partition_id;
} obj_id_t;

/* A page node in the linked list of object-mapped pages */
typedef struct page_node
{
    uint32_t page_id;
    object_id_t object_id;
    struct page_node *next;
    UT_hash_handle hh; /* makes this structure hashable */
} page_node;

/* The object struct. Metadata will be added as a pointer to another struct or as more fields */
typedef struct stored_object
{
    object_id_t id;
    size_t size;
    page_node *pages;
    UT_hash_handle hh; /* makes this structure hashable */
} stored_object;

/* struct which will hold the ids of existing objects */
typedef struct object_map
{
    object_id_t id;
    bool exists;
    UT_hash_handle hh; /* makes this structure hashable */
} object_map;

typedef struct obj_strategy_manager {
    stored_object *objects_table;
    object_map *objects_mapping;
    page_node *global_page_table;
    object_id_t current_id;
    struct osd_device osd;
    uint8_t *osd_sense;
    bool initialized;
} obj_strategy_manager_t;

extern obj_strategy_manager_t *obj_manager;

void INIT_OBJ_STRATEGY(uint8_t device_index);
void TERM_OBJ_STRATEGY(uint8_t device_index);

// Object write strategy API functions to be called by QEMU
ftl_ret_val FTL_OBJ_READ(uint8_t device_index, obj_id_t object_loc, void *data, offset_t offset, length_t *length);
ftl_ret_val FTL_OBJ_WRITE(uint8_t device_index, obj_id_t object_loc, const void *data, offset_t offset, length_t length);
bool FTL_OBJ_CREATE(uint8_t device_index, obj_id_t obj_loc, size_t size);
ftl_ret_val FTL_OBJ_DELETE(uint8_t device_index, obj_id_t object_loc);
ftl_ret_val FTL_OBJ_LIST(uint8_t device_index, void *data, size_t *size, uint64_t initial_oid);

/* FTL functions */
ftl_ret_val _FTL_OBJ_READ(uint8_t device_index, obj_id_t object_loc, void *data, offset_t offset, length_t *length);
ftl_ret_val _FTL_OBJ_WRITE(uint8_t device_index, obj_id_t object_loc, const void *data, offset_t offset, length_t length);
ftl_ret_val _FTL_OBJ_COPYBACK(uint8_t device_index, int32_t source, int32_t destination);
bool _FTL_OBJ_CREATE(uint8_t device_index, obj_id_t obj_loc, size_t size);
ftl_ret_val _FTL_OBJ_DELETE(uint8_t device_index, obj_id_t object_loc);
ftl_ret_val _FTL_OBJ_LIST(uint8_t device_index, void *data, size_t *size, uint64_t initial_oid);

/* Helper functions */
stored_object *lookup_object(uint8_t device_index, object_id_t object_id);
object_map *lookup_object_mapping(uint8_t device_index, object_id_t object_id);
stored_object *create_object(uint8_t device_index, object_id_t obj_id, size_t size);
int remove_object(uint8_t device_index, stored_object *object, object_map *obj_map);

page_node *allocate_new_page(object_id_t object_id, uint32_t page_id);
page_node *add_page(uint8_t device_index, stored_object *object, uint32_t page_id);
page_node *page_by_offset(uint8_t device_index, stored_object *object, unsigned int offset);
page_node *lookup_page(uint8_t device_index, uint32_t page_id);
page_node *next_page(stored_object *object, page_node *current);
void free_obj_table(uint8_t device_index);
void free_page_table(uint8_t device_index);
void free_obj_mapping(uint8_t device_index);

#endif
