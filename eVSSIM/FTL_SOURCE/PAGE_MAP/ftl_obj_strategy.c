#include <assert.h>

#include "common.h"
#include "ftl_obj_strategy.h"

#include "osc-osd/osd-target/osd.h"
#include "osc-osd/osd-util/osd-util.h"
#include "osc-osd/osd-util/osd-defs.h"

stored_object *objects_table = NULL;
object_map *objects_mapping = NULL;
page_node *global_page_table = NULL;
object_id_t current_id;

static struct osd_device osd = { 0x0 };
static uint8_t *osd_sense = NULL;
#define OSD_READ_VALUE_OFFSET       (44)
#define OSD_SENSE_BUFFER_SIZE       (1024)

#ifndef MIN
#define MIN(x, y) ((x) > (y) ? (y) : (x))
#endif

//todo: fix object page add and copyback so that the occupied pages in ssd_io_manager.c will be updated

void INIT_OBJ_STRATEGY(void)
{
    current_id = 1;
    objects_table = NULL;
    objects_mapping = NULL;
    global_page_table = NULL;

    const char *root = "/tmp/osd/";
    assert(!system("rm -rf /tmp/osd"));
    assert(!osd_open(root, &osd));
    osd_sense = (uint8_t*)malloc(OSD_SENSE_BUFFER_SIZE);
    memset(osd_sense, 0x0, OSD_SENSE_BUFFER_SIZE);

    // creating a single partition, to be used later to store all
    // user objects
    assert(!osd_create_partition(&osd, PARTITION_PID_LB, 0, osd_sense));
}

void free_obj_table(void)
{
    struct stored_object *current_object, *tmp;

    HASH_ITER(hh, objects_table, current_object, tmp)
    {
        HASH_DEL(objects_table, current_object);
        free(current_object);
    }
}

void free_obj_mapping(void)
{
    struct object_map *current_mapping, *tmp;

    HASH_ITER(hh, objects_mapping, current_mapping, tmp)
    {
        HASH_DEL(objects_mapping, current_mapping);
        free(current_mapping);
    }
}

void free_page_table(void)
{
    struct page_node *current_page, *tmp;

    HASH_ITER(hh, global_page_table, current_page, tmp)
    {
        HASH_DEL(global_page_table, current_page);
        free(current_page);
    }
}

void TERM_OBJ_STRATEGY(void)
{
    free_obj_table();
    free_obj_mapping();
    free_page_table();

    if (osd_sense != NULL) {
        free(osd_sense);
        osd_sense = NULL;
        osd_close(&osd);
    }
}

ftl_ret_val _FTL_OBJ_READ(uint8_t device_index, obj_id_t obj_loc, void *data, offset_t offset, length_t *p_length)
{
    stored_object *object;
    page_node *current_page;
    int io_page_nb;
    int curr_io_page_nb;
    unsigned int ret = FTL_FAILURE;
    int osd_ret;

    if (p_length == NULL) {
        PDBG_FTL("Invalid null ptr.\n");
        return FTL_FAILURE;
    }

    length_t length = *p_length;

    object = lookup_object(obj_loc.object_id);

    // file not found
    if (object == NULL)
        return FTL_FAILURE;

    if (object->size == 0) {
        *p_length = 0;
        PDBG_FTL("Complete\n");
        return FTL_SUCCESS;
    }

    // object not big enough
    if (object->size < (offset + length))
        return FTL_FAILURE;

    if (!(current_page = page_by_offset(device_index, object, offset)))
    {
        RERR(FTL_FAILURE, "%u lookup page by offset failed \n", current_page->page_id);
    }

    // just calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna read
    io_alloc_overhead = ALLOC_IO_REQUEST(device_index, current_page->page_id * devices[device_index].sectors_per_page, length, READ, &io_page_nb);

    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        // simulate the page read
        ret = SSD_PAGE_READ(device_index, CALC_FLASH(device_index, current_page->page_id), 
                    CALC_BLOCK(device_index, current_page->page_id),
                    CALC_PAGE(device_index, current_page->page_id),
                    curr_io_page_nb, READ);

        // send a physical read action being done to the statistics gathering
        if (ret == FTL_SUCCESS)
        {
            FTL_STATISTICS_GATHERING(device_index, current_page->page_id, PHYSICAL_READ);
        }
        // TODO: fix
        if (ret == FTL_FAILURE)
        {
            PDBG_FTL("Error %u page read fail \n", current_page->page_id);
        }

        // get the next page
        current_page = current_page->next;
    }

    INCREASE_IO_REQUEST_SEQ_NB();

    if (data != NULL) {
        uint64_t outlen = 0;
        osd_ret = osd_read(&osd, obj_loc.partition_id, obj_loc.object_id,
                    length, 0, NULL, data, &outlen, 0, osd_sense, DDT_CONTIG);
        if (osd_ret < 0) {
            PDBG_FTL("osd_read failed with ret: %d.\n", osd_ret);
            return FTL_FAILURE;
        }

        *p_length = get_ntohll(osd_sense + OSD_READ_VALUE_OFFSET);
        if (length < *p_length) *p_length = length;

        memset(osd_sense, 0x0, OSD_SENSE_BUFFER_SIZE);
    }

    PDBG_FTL("Complete\n");

    return ret;
}

ftl_ret_val _FTL_OBJ_WRITE(uint8_t device_index, obj_id_t object_loc, const void *data, offset_t offset, length_t length)
{
    stored_object *object;
    page_node *current_page = NULL, *temp_page;
    uint64_t page_id;
    int io_page_nb;
    int curr_io_page_nb;
    unsigned int ret = FTL_SUCCESS;
    int osd_ret;

    object = lookup_object(object_loc.object_id);

    // file not found
    if (object == NULL)
        RERR(FTL_FAILURE, "failed lookup\n");

    // calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna write
    io_alloc_overhead = ALLOC_IO_REQUEST(device_index, offset, length, WRITE, &io_page_nb);

    // if the offset is past the current size of the stored_object we need to append new pages until we can start writing
    while (offset > object->size)
    {
        if (GET_NEW_PAGE(VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &page_id) == FTL_FAILURE)
        {
            // not enough memory presumably
            RERR(FTL_FAILURE, "[FTL_WRITE] Get new page fail \n");
        }
        if (!add_page(device_index, object, page_id))
            return FTL_FAILURE;

        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(device_index, page_id);
    }

    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        // if this is the first iteration we need to find the page by offset, otherwise we can go with the page chain
        if (current_page == NULL)
            current_page = page_by_offset(device_index, object, offset);
        else
            current_page = current_page->next;

        // get the pge we'll be writing to
        if (GET_NEW_PAGE(VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &page_id) == FTL_FAILURE)
        {
            RERR(FTL_FAILURE, "[FTL_WRITE] Get new page fail \n");
        }
        if ((temp_page = lookup_page(page_id)))
        {
            RERR(FTL_FAILURE, "[FTL_WRITE] Object %lu already contains page %lu\n", temp_page->object_id, page_id);
        }

        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(device_index, page_id);

        if (current_page == NULL) // writing at the end of the object and need to allocate more space for it
        {
            current_page = add_page(device_index, object, page_id);
            if (!current_page)
                return FTL_FAILURE;
        }
        else // writing over parts of the object
        {
            // invalidate the old physical page and replace the page_node's page
            UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(device_index, current_page->page_id),
                CALC_BLOCK(device_index, current_page->page_id),
                CALC_PAGE(device_index, current_page->page_id),
                PAGE_INVALID);
            UPDATE_INVERSE_PAGE_MAPPING(current_page->page_id, MAPPING_TABLE_INIT_VAL);

            HASH_DEL(global_page_table, current_page);
            current_page->page_id = page_id;
            HASH_ADD_INT(global_page_table, page_id, current_page);
        }
#ifdef GC_ON
        // must improve this because it is very possible that we will do multiple GCs on the same flash chip and block
        // probably gonna add an array to hold the unique ones and in the end GC all of them

        // GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), false, true);
#endif

        ret = SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, page_id),
            CALC_BLOCK(device_index, page_id), CALC_PAGE(device_index, page_id), curr_io_page_nb, WRITE);

        // send a physical write action being done to the statistics gathering
        if (ret == FTL_SUCCESS)
        {
            FTL_STATISTICS_GATHERING(device_index, page_id, PHYSICAL_WRITE);
        }

        // TODO: fix
        if (ret == FTL_FAILURE)
        {
            PDBG_FTL("Error[FTL_WRITE] %lu page write fail \n", page_id);
        }

        //        page_node *page;
        //        printf("Object page map:{");
        //        for(page=object->pages; page; page=page->next)
        //            printf("%d->",page->page_id);
        //        printf("}\n");
    }

    if (data != NULL) {
        osd_ret = osd_write(&osd, object_loc.partition_id, object_loc.object_id,
            length, offset, (uint8_t *)data, 0, osd_sense, DDT_CONTIG);
        if (osd_ret < 0) {
            PDBG_FTL("Failed to osd_write with ret: %d\n", osd_ret);
            return FTL_FAILURE;
        }
    }

    INCREASE_IO_REQUEST_SEQ_NB();

    PDBG_FTL("Complete\n");

    return ret;
}

ftl_ret_val _FTL_OBJ_COPYBACK(uint8_t device_index, int32_t source, int32_t destination)
{
    page_node *source_p;

    source_p = lookup_page(source);

    // source_p can be NULL if the GC is working on some old pages that belonged to an object we deleted already
    if (source_p != NULL)
    {
        // invalidate the source page
        UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(device_index, source),
            CALC_BLOCK(device_index, source), CALC_PAGE(device_index, source), PAGE_INVALID);

        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(device_index, destination);

        // change the object's page mapping to the new page
        HASH_DEL(global_page_table, source_p);
        source_p->page_id = destination;
        HASH_ADD_INT(global_page_table, page_id, source_p);
    }
    else
    {
        PDBG_FTL("Warning %u copyback page not mapped to an object \n", source);
        return FTL_FAILURE;
    }

    return FTL_SUCCESS;
}

bool _FTL_OBJ_CREATE(uint8_t device_index, obj_id_t obj_loc, size_t size)
{
    stored_object *new_object;
    int osd_ret;


    new_object = create_object(device_index, obj_loc.object_id, size);

    if (new_object == NULL)
    {
        return false;
    }

    osd_ret = osd_create(&osd, obj_loc.partition_id, obj_loc.object_id, 1, 0, osd_sense);
    if (osd_ret < 0) {
        if (_FTL_OBJ_DELETE(device_index, obj_loc) != FTL_SUCCESS) {
            PDBG_FTL("Warning! couldn't delete object.\n");
        }

        PDBG_FTL("Failed to osd_create with ret: %d\n", osd_ret);
        return false;
    }

    return true;
}

ftl_ret_val _FTL_OBJ_DELETE(uint8_t device_index, obj_id_t obj_loc)
{
    stored_object *object;
    object_map *obj_map;
    int osd_ret;

    object = lookup_object(obj_loc.object_id);

    // object not found
    if (object == NULL)
        return FTL_FAILURE;

    obj_map = lookup_object_mapping(obj_loc.object_id);

    // object_map not found
    if (obj_map == NULL)
        return FTL_FAILURE;

    osd_ret = osd_remove(&osd, obj_loc.partition_id, obj_loc.object_id, 0, osd_sense);
    if (osd_ret < 0) {
        PDBG_FTL("Failed to remove OSD object with ret: %d.\n", osd_ret);
        return FTL_FAILURE;
    }

    return remove_object(device_index, object, obj_map);
}

ftl_ret_val _FTL_OBJ_LIST(void *data, size_t *size, uint64_t initial_oid)
{
    int osd_ret;
    struct getattr_list get_attr = {
        .sz = 0,
        .le = 0
    };

    if (data == NULL || size == NULL) {
        PDBG_FTL("Invalid null ptr.\n");
        return FTL_FAILURE;
    }

    osd_ret = osd_list(&osd, 0, USEROBJECT_PID_LB, *size, initial_oid, &get_attr,
        0, data, size, osd_sense);
    if (osd_ret < 0) {
        printf("failed to execute osd_list\n");
        return FTL_FAILURE;
    }

    return FTL_SUCCESS;
}

stored_object *lookup_object(object_id_t object_id)
{
    stored_object *object;

    // try to find it in our hashtable. NULL will be returned if key not found
    HASH_FIND_INT(objects_table, &object_id, object);

    return object;
}

object_map *lookup_object_mapping(object_id_t object_id)
{
    object_map *obj_map;

    // try to find it in our hashtable. NULL will be returned if key not found
    HASH_FIND_INT(objects_mapping, &object_id, obj_map);

    return obj_map;
}

stored_object *create_object(uint8_t device_index, object_id_t obj_id, size_t size)
{
    stored_object *obj = malloc(sizeof(stored_object));
    uint64_t page_id;

    object_map *obj_map;

    HASH_FIND_INT(objects_mapping, &obj_id, obj_map);
    //if the requested id was not found, let's add it
    if (obj_map == NULL)
    {
        object_map *new_obj_id = (object_map *)malloc(sizeof(object_map));
        new_obj_id->id = obj_id;
        new_obj_id->exists = true;
        HASH_ADD_INT(objects_mapping, id, new_obj_id);
    }
    else
    {
        RINFO(NULL, "Object %lu already exists, cannot create it !\n", obj_id);
    }

    // initialize to stored_object struct with size and initial pages
    obj->id = obj_id;
    obj->size = 0;
    obj->pages = NULL;

    // add the new object to the objects' hashtable
    HASH_ADD_INT(objects_table, id, obj);

    while (size > obj->size)
    {
        if (GET_NEW_PAGE(VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &page_id) == FTL_FAILURE)
        {
            // cleanup just in case we managed to do anything up until now
            remove_object(device_index, obj, obj_map);
            return NULL;
        }

        if (!add_page(device_index, obj, page_id))
            return NULL;

        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(device_index, page_id);
    }

    return obj;
}

int remove_object(uint8_t device_index, stored_object *object, object_map *obj_map)
{
    page_node *current_page;
    page_node *invalidated_page;

    if (object == NULL)
        return FTL_SUCCESS;

    // object could not exist in the hashtable yet because it could just be cleanup in case create_object failed
    // if we do perform HASH_DEL on an object that is not in the hashtable, the whole hashtable will be deleted
    if (object && (object->hh.tbl != NULL))
        HASH_DEL(objects_table, object);

    if (obj_map && (obj_map->hh.tbl != NULL))
    {
        HASH_DEL(objects_mapping, obj_map);
        free(obj_map);
    }

    current_page = object->pages;
    while (current_page != NULL)
    {
        // invalidate the physical page and update its mapping
        UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(device_index, current_page->page_id),
            CALC_BLOCK(device_index, current_page->page_id), CALC_PAGE(device_index, current_page->page_id), PAGE_INVALID);

#ifdef GC_ON
        // should we really perform GC for every page? we know we are invalidating a lot of them now...
        // GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), true, true);
#endif

        // get next page and free the current one
        invalidated_page = current_page;
        current_page = current_page->next;

        if (invalidated_page->hh.tbl != NULL)
            HASH_DEL(global_page_table, invalidated_page);

        free(invalidated_page);
    }

    // free the object's memory
    free(object);

    return FTL_SUCCESS;
}

page_node *allocate_new_page(object_id_t object_id, uint32_t page_id)
{
    page_node *page = malloc(sizeof(struct page_node));
    page->page_id = page_id;
    page->object_id = object_id;
    page->next = NULL;
    return page;
}

page_node *add_page(uint8_t device_index, stored_object *object, uint32_t page_id)
{
    page_node *curr, *page;

    HASH_FIND_INT(global_page_table, &page_id, page);
    if (page)
    {
        RERR(NULL, "[add_page] Object %lu already contains page %d\n", page->object_id, page_id);
    }

    object->size += GET_PAGE_SIZE(device_index);
    if (object->pages == NULL)
    {
        page = allocate_new_page(object->id, page_id);
        HASH_ADD_INT(global_page_table, page_id, page);

        object->pages = page;
        return object->pages;
    }
    for (curr = page = object->pages; page && page->page_id != page_id; curr = page, page = page->next)
        ;
    if (page)
    {
        RERR(NULL, "[add_page] Object %lu already contains page %d\n", page->object_id, page_id);
    }

    page = allocate_new_page(object->id, page_id);

    HASH_ADD_INT(global_page_table, page_id, page);
    curr->next = page;
    return curr->next;
}

page_node *page_by_offset(uint8_t device_index, stored_object *object, unsigned int offset)
{
    page_node *page;

    // check if out of bounds
    if (offset > object->size)
        return NULL;

    // skim through pages until offset is less than a page's size
    for (page = object->pages; page && offset >= GET_PAGE_SIZE(device_index); offset -= GET_PAGE_SIZE(device_index), page = page->next)
        ;

    // if page==NULL then page collection < size - report error? or assume it's valid? - this technically shouldn't happen after the if at the beginning. just return NULL if it does
    return page;
}

page_node *lookup_page(uint32_t page_id)
{
    page_node *page;
    HASH_FIND_INT(global_page_table, &page_id, page);
    return page;
}
