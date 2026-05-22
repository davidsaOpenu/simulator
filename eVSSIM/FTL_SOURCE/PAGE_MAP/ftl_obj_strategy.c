#include <assert.h>

#include "common.h"
#include "ftl_obj_strategy.h"

#include "osc-osd/osd-target/osd.h"
#include "osc-osd/osd-util/osd-util.h"
#include "osc-osd/osd-util/osd-defs.h"

stored_object*** objects_table = NULL;
object_map*** objects_mapping = NULL;
page_node** global_page_table = NULL;

#define OSD_READ_VALUE_OFFSET       (44)
#define OSD_SENSE_BUFFER_SIZE       (1024)

static struct osd_device osd = { 0x0 };
static uint8_t osd_sense[OSD_SENSE_BUFFER_SIZE];
static bool is_osd_init = false;

#ifndef MIN
#define MIN(x, y) ((x) > (y) ? (y) : (x))
#endif

//todo: fix object page add and copyback so that the occupied pages in ssd_io_manager.c will be updated

void INIT_OBJ_STRATEGY(uint8_t device_index)
{
    uint32_t namespaceIndex = 0;
	for (namespaceIndex = 0; namespaceIndex < MAX_NUMBER_OF_NAMESPACES; namespaceIndex++)
	{
		/* Clean the tables */
		objects_table[device_index][namespaceIndex] = NULL;
		objects_mapping[device_index][namespaceIndex] = NULL;
    }

    global_page_table[device_index] = NULL;

    if (!is_osd_init)
    {
        const char *root = "/tmp/osd/";
        assert(!system("rm -rf /tmp/osd"));
        assert(!osd_open(root, &osd));

        // Clean the buffer.
        memset(osd_sense, 0x0, OSD_SENSE_BUFFER_SIZE);

        // creating a single partition, to be used later to store all user objects
        assert(!osd_create_partition(&osd, PARTITION_PID_LB, 0, osd_sense));

        is_osd_init = true;
    }
}

void free_obj_table(uint8_t device_index)
{
    struct stored_object *tmp = NULL;
    struct stored_object *current_object = NULL;

    uint32_t namespaceIndex = 0;
	for (namespaceIndex = 0; namespaceIndex < MAX_NUMBER_OF_NAMESPACES; namespaceIndex++)
	{
        HASH_ITER(hh, objects_table[device_index][namespaceIndex], current_object, tmp)
        {
            HASH_DEL(objects_table[device_index][namespaceIndex], current_object);
            free(current_object);
        }
    }
}

void free_obj_mapping(uint8_t device_index)
{
    struct object_map *tmp = NULL;
    struct object_map *current_mapping = NULL;

    uint32_t namespaceIndex = 0;
	for (namespaceIndex = 0; namespaceIndex < MAX_NUMBER_OF_NAMESPACES; namespaceIndex++)
	{
        HASH_ITER(hh, objects_mapping[device_index][namespaceIndex], current_mapping, tmp)
        {
            HASH_DEL(objects_mapping[device_index][namespaceIndex], current_mapping);
            free(current_mapping);
        }
    }
}

void free_page_table(uint8_t device_index)
{
    struct page_node *tmp = NULL;
    struct page_node *current_page = NULL;

    HASH_ITER(hh, global_page_table[device_index], current_page, tmp)
    {
        HASH_DEL(global_page_table[device_index], current_page);
        free(current_page);
    }
}

void TERM_OBJ_STRATEGY(uint8_t device_index)
{
    free_obj_table(device_index);
    free_obj_mapping(device_index);
    free_page_table(device_index);

    if (is_osd_init) {
        osd_close(&osd);
        is_osd_init = false;
    }
}

ftl_ret_val _FTL_OBJ_READ(uint8_t device_index, uint32_t nsid, obj_id_t obj_loc, void *data, offset_t offset, length_t *p_length)
{
	if (devices[device_index].namespaces[nsid].nsid != nsid ||
		devices[device_index].namespaces[nsid].type != FTL_NS_OBJECT) {
		RERR(FTL_FAILURE, "Can't read from invalid namespace, device_index: %u, nsid: %u\n", device_index, nsid);
	}

    page_node *current_page = NULL;
    int io_page_nb;
    int curr_io_page_nb;

    int osd_ret;

    stored_object *object = lookup_object(device_index, nsid, obj_loc.object_id);

    // The object isn't found.
    if (object == NULL)
        RERR(FTL_FAILURE, "failed lookup object\n");

    if (p_length == NULL) {
        PDBG_FTL("Invalid length ptr is null.\n");
        return FTL_FAILURE;
    }

    // Handle empty read request.
    if (object->size == 0) {
        *p_length = 0;

        PDBG_FTL("Complete\n");
        return FTL_SUCCESS;
    }

    length_t length = *p_length;

    // The object isn't big enough
    if (object->size < (offset + length))
        return FTL_FAILURE;

    current_page = page_by_offset(GET_PAGE_SIZE(device_index), object, offset);

    if (current_page == NULL) {
        RERR(FTL_FAILURE, "Lookup page by offset failed.\n");
    }

    if (current_page->nsid != nsid) {
        RERR(FTL_FAILURE, "Page invalide namespace id.\n");
    }

    // just calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna read
    ssds_manager[device_index].io_alloc_overhead = ALLOC_IO_REQUEST(device_index, current_page->page_id * devices[device_index].sectors_per_page, length, READ, &io_page_nb);

    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        if (current_page == NULL)
            RERR(FTL_FAILURE, "Failed to read object pages.\n");

        // Simulate the page read.
        if (SSD_PAGE_READ(device_index, CALC_FLASH(device_index, current_page->page_id), CALC_BLOCK(device_index, current_page->page_id),
                          CALC_PAGE(device_index, current_page->page_id), curr_io_page_nb, READ) == FTL_SUCCESS) {

            // send a physical read action being done to the statistics gathering
            FTL_STATISTICS_GATHERING(device_index, current_page->page_id, PHYSICAL_READ);

        } else {
            PDBG_FTL("Error %u page read fail.\n", current_page->page_id);
        }

        // get the next page
        current_page = current_page->next;
    }

    INCREASE_IO_REQUEST_SEQ_NB();

    if (data != NULL) {
        uint64_t outlen = 0;
        osd_ret = osd_read(&osd, obj_loc.partition_id, obj_loc.object_id, length,
                           0, NULL, data, &outlen, 0, osd_sense, DDT_CONTIG);

        if (osd_ret < 0) {
            PDBG_FTL("osd_read failed with ret: %d.\n", osd_ret);
            return FTL_FAILURE;
        }

        *p_length = get_ntohll(osd_sense + OSD_READ_VALUE_OFFSET);

        if (length < *p_length) {
            *p_length = length;
        }

        memset(osd_sense, 0x0, OSD_SENSE_BUFFER_SIZE);
    }

    PDBG_FTL("Complete\n");
    return FTL_SUCCESS;
}

ftl_ret_val FTL_OBJ_READ(uint8_t device_index, uint32_t nsid, obj_id_t obj_loc, void *data, offset_t offset, length_t *p_length)
{
	pthread_mutex_lock(&g_lock);
	ftl_ret_val ret = _FTL_OBJ_READ(device_index, nsid, obj_loc, data, offset, p_length);
	pthread_mutex_unlock(&g_lock);
	return ret;
}

ftl_ret_val _FTL_OBJ_WRITE(uint8_t device_index, uint32_t nsid, obj_id_t object_loc, const void *data, offset_t offset, length_t length)
{
	if (devices[device_index].namespaces[nsid].nsid != nsid ||
		devices[device_index].namespaces[nsid].type != FTL_NS_OBJECT) {
		RERR(FTL_FAILURE, "Can't write from invalid namespace, device_index: %u, nsid: %u\n", device_index, nsid);
	}

    page_node *current_page = NULL;
    uint64_t page_id;

    int io_page_nb;
    int curr_io_page_nb;

    int osd_ret;

    stored_object *object = lookup_object(device_index, nsid, object_loc.object_id);

    // The object isn't found.
    if (object == NULL)
        RERR(FTL_FAILURE, "failed lookup object\n");

    // calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna write
    ssds_manager[device_index].io_alloc_overhead = ALLOC_IO_REQUEST(device_index, offset, length, WRITE, &io_page_nb);

    // If the offset is past the current size of the stored_object we need to append new pages until we can start writing.
    if (!expend_object(device_index, nsid, object, offset)){
        return FTL_FAILURE;
    }

    // Find the page by offset in the object.
    current_page = page_by_offset(GET_PAGE_SIZE(device_index), object, offset);

    if (current_page->nsid != nsid) {
        RERR(FTL_FAILURE, "Page invalide namespace id.\n");
    }

    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        // Get a new page ID for writing.
        if (GET_NEW_PAGE(device_index, VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &page_id) == FTL_FAILURE) {
            RERR(FTL_FAILURE, "[FTL_WRITE] Get new page fail \n");
        }

        if (lookup_page(device_index, page_id) != NULL)
            RERR(FTL_FAILURE, "[FTL_WRITE] Failed to get new page.\n");

        // writing at the end of the object and need to allocate more space for it.
        if (current_page == NULL)
        {
            current_page = add_page(device_index, nsid, object, page_id);

            if (current_page == NULL)
                RERR(FTL_FAILURE, "Failed to add page into object");

            // mark new page as valid and used.
            UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(device_index, page_id);
        }
        // writing over page of the object.
        else
        {
            // invalidate the old physical page and replace the page_node's page
            UPDATE_INVERSE_BLOCK_VALIDITY(device_index, CALC_FLASH(device_index, current_page->page_id),
                CALC_BLOCK(device_index, current_page->page_id),
                CALC_PAGE(device_index, current_page->page_id),
                PAGE_INVALID);

            UPDATE_INVERSE_PAGE_MAPPING(device_index, current_page->page_id, INVALID_NSID, MAPPING_TABLE_INIT_VAL);

            HASH_DEL(global_page_table[device_index], current_page);
            current_page->page_id = page_id;
            HASH_ADD_INT(global_page_table[device_index], page_id, current_page);
        }

#ifdef GC_ON
        // must improve this because it is very possible that we will do multiple GCs on the same flash chip and block
        // probably gonna add an array to hold the unique ones and in the end GC all of them

        // GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), false, true);
#endif

        // send a physical write action being done to the statistics gathering
        if (SSD_PAGE_WRITE(device_index, CALC_FLASH(device_index, page_id), CALC_BLOCK(device_index, page_id), CALC_PAGE(device_index, page_id), curr_io_page_nb, WRITE) == FTL_SUCCESS) {

            FTL_STATISTICS_GATHERING(device_index, page_id, PHYSICAL_WRITE);

        }
        else {
            PDBG_FTL("Error[FTL_WRITE] %lu page write fail \n", page_id);
        }

        // Go to the next page in the object.
        // NOTE: in the last page it will expend the object with new page.
        current_page = current_page->next;
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
    return FTL_SUCCESS;
}

ftl_ret_val FTL_OBJ_WRITE(uint8_t device_index, uint32_t nsid, obj_id_t object_loc, const void *data, offset_t offset, length_t length)
{
	pthread_mutex_lock(&g_lock);
	ftl_ret_val ret = _FTL_OBJ_WRITE(device_index, nsid, object_loc, data, offset, length);
	pthread_mutex_unlock(&g_lock);
	return ret;
}

ftl_ret_val _FTL_OBJ_COPYBACK(uint8_t device_index, int32_t source, int32_t destination)
{
    page_node *source_page = lookup_page(device_index, source);

    // source_page can be NULL if the GC is working on some old pages that belonged to an object we deleted already.
    if (source_page == NULL)
    {
        PDBG_FTL("Warning %u copyback page not mapped to an object \n", source);
        return FTL_FAILURE;
    }

    // invalidate the source page
    UPDATE_INVERSE_BLOCK_VALIDITY(device_index, CALC_FLASH(device_index, source), CALC_BLOCK(device_index, source), CALC_PAGE(device_index, source), PAGE_INVALID);

    // mark new page as valid and used
    UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(device_index, destination);

    // change the object's page mapping to the new page
    HASH_DEL(global_page_table[device_index], source_page);

    source_page->page_id = destination;
    HASH_ADD_INT(global_page_table[device_index], page_id, source_page);

    return FTL_SUCCESS;
}

bool _FTL_OBJ_CREATE(uint8_t device_index, uint32_t nsid, obj_id_t obj_loc, size_t size)
{
	if (devices[device_index].namespaces[nsid].nsid != nsid ||
		devices[device_index].namespaces[nsid].type != FTL_NS_OBJECT) {
		RERR(false, "Can't create object in invalid namespace, device_index: %u, nsid: %u\n", device_index, nsid);
	}

    int osd_ret;

    if (create_object(device_index, nsid, obj_loc.object_id, size) == NULL)
    {
        return false;
    }

    osd_ret = osd_create(&osd, obj_loc.partition_id, obj_loc.object_id, 1, 0, osd_sense);

    if (osd_ret < 0) {
        if (_FTL_OBJ_DELETE(device_index, nsid, obj_loc) != FTL_SUCCESS) {
            PDBG_FTL("Warning! couldn't delete object.\n");
        }

        PDBG_FTL("Failed to osd_create with ret: %d\n", osd_ret);
        return false;
    }

    return true;
}

bool FTL_OBJ_CREATE(uint8_t device_index, uint32_t nsid, obj_id_t obj_loc, size_t size)
{
	pthread_mutex_lock(&g_lock);
	bool ret = _FTL_OBJ_CREATE(device_index, nsid, obj_loc, size);
	pthread_mutex_unlock(&g_lock);
	return ret;
}

ftl_ret_val _FTL_OBJ_DELETE(uint8_t device_index, uint32_t nsid, obj_id_t obj_loc)
{
	if (devices[device_index].namespaces[nsid].nsid != nsid ||
		devices[device_index].namespaces[nsid].type != FTL_NS_OBJECT) {
		RERR(FTL_FAILURE, "Can't delete from invalid namespace, device_index: %u, nsid: %u\n", device_index, nsid);
	}

    int osd_ret;
    ftl_ret_val ret = FTL_FAILURE;

    stored_object *object = lookup_object(device_index, nsid, obj_loc.object_id);
    object_map *obj_map = lookup_object_mapping(device_index, nsid, obj_loc.object_id);

    if (object == NULL || obj_map == NULL)
        return FTL_FAILURE;

    ret = remove_object(device_index, nsid, object, obj_map);

    osd_ret = osd_remove(&osd, obj_loc.partition_id, obj_loc.object_id, 0, osd_sense);

    if (osd_ret < 0) {
        PDBG_FTL("Failed to remove OSD object with ret: %d.\n", osd_ret);
        return FTL_FAILURE;
    }

    return ret;
}

ftl_ret_val FTL_OBJ_DELETE(uint8_t device_index, uint32_t nsid, obj_id_t obj_loc)
{
	pthread_mutex_lock(&g_lock);
	ftl_ret_val ret = _FTL_OBJ_DELETE(device_index, nsid, obj_loc);
	pthread_mutex_unlock(&g_lock);
	return ret;
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
        PDBG_FTL("failed to execute osd_list\n");
        return FTL_FAILURE;
    }

    return FTL_SUCCESS;
}

ftl_ret_val FTL_OBJ_LIST(void *data, size_t *size, uint64_t initial_oid)
{
	pthread_mutex_lock(&g_lock);
    ftl_ret_val ret = _FTL_OBJ_LIST(data, size, initial_oid);
	pthread_mutex_unlock(&g_lock);
	return ret;
}

stored_object *lookup_object(uint8_t device_index, uint32_t nsid, object_id_t object_id)
{
    stored_object *object = NULL;

    // try to find it in our hashtable. NULL will be returned if key not found
    HASH_FIND_INT(objects_table[device_index][nsid], &object_id, object);

    return object;
}

object_map *lookup_object_mapping(uint8_t device_index, uint32_t nsid, object_id_t object_id)
{
    object_map *obj_map = NULL;

    // try to find it in our hashtable. NULL will be returned if key not found
    HASH_FIND_INT(objects_mapping[device_index][nsid], &object_id, obj_map);

    return obj_map;
}

stored_object *create_object(uint8_t device_index, uint32_t nsid, object_id_t obj_id, size_t size)
{
	if (devices[device_index].namespaces[nsid].nsid != nsid ||
		devices[device_index].namespaces[nsid].type != FTL_NS_OBJECT) {
		RERR(NULL, "Failed to create at invalid namespace, device_index: %u, nsid: %u\n", device_index, nsid);
	}

    if (lookup_object_mapping(device_index, nsid, obj_id) != NULL)
    {
        RINFO(NULL, "Object %lu already exists, cannot create it!\n", obj_id);
    }

    // if the requested id was not found, let's add it.
    stored_object *obj = (stored_object *)malloc(sizeof(stored_object));
    object_map *new_obj_id = (object_map *)malloc(sizeof(object_map));

    new_obj_id->id = obj_id;
    new_obj_id->exists = true;
    HASH_ADD_INT(objects_mapping[device_index][nsid], id, new_obj_id);

    // Initialize to stored_object struct.
    obj->id = obj_id;
    obj->size = 0;
    obj->pages = NULL;

    // add the new object to the objects' hashtable.
    HASH_ADD_INT(objects_table[device_index][nsid], id, obj);

    if (!expend_object(device_index, nsid, obj, size)){
        // Failed to init the object size.
        remove_object(device_index, nsid, obj, new_obj_id);
        return NULL;
    }

    return obj;
}

bool expend_object(uint8_t device_index, uint32_t nsid, stored_object *object, size_t size)
{
	if (devices[device_index].namespaces[nsid].nsid != nsid ||
		devices[device_index].namespaces[nsid].type != FTL_NS_OBJECT) {
		RERR(false, "Can't expend object at invalid namespace, device_index: %u, nsid: %u\n", device_index, nsid);
	}

    uint64_t page_id;

    // If the new size is past the current size of the object, expend it.
    while (size > object->size)
    {
        if (GET_NEW_PAGE(device_index, VICTIM_OVERALL, devices[device_index].empty_table_entry_nb, &page_id) == FTL_FAILURE) {
            // not enough memory presumably
            RERR(false, "[FTL_WRITE] Failed to get new page.\n");
        }

        if (add_page(device_index, nsid, object, page_id) == NULL) {
            return false;
        }

        // Mark new page as valid and used.
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(device_index, page_id);
    }

    return true;
}

int remove_object(uint8_t device_index, uint32_t nsid, stored_object *object, object_map *obj_map)
{
    page_node *current_page = NULL;

    // Checks if object exist in the hashtable.
    // if we do perform HASH_DEL on an object that is not in the hashtable, the whole hashtable will be deleted.
    if ((obj_map != NULL) && (obj_map->hh.tbl != NULL))
    {
        HASH_DEL(objects_mapping[device_index][nsid], obj_map);
        free(obj_map);
    }

    if (object == NULL)
        return FTL_SUCCESS;

    if (object->hh.tbl != NULL)
        HASH_DEL(objects_table[device_index][nsid], object);

    current_page = object->pages;
    while (current_page != NULL)
    {
        // invalidate the physical page and update its mapping
        UPDATE_INVERSE_BLOCK_VALIDITY(device_index, CALC_FLASH(device_index, current_page->page_id),
                                      CALC_BLOCK(device_index, current_page->page_id), CALC_PAGE(device_index, current_page->page_id), PAGE_INVALID);

#ifdef GC_ON
        // should we really perform GC for every page? we know we are invalidating a lot of them now...
        // GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), true, true);
#endif // GC_ON

        // Remove this page and get next page.
        current_page = remove_page(device_index, current_page);
    }

    // free the object's memory.
    free(object);

    return FTL_SUCCESS;
}

page_node *allocate_new_page(uint8_t device_index, uint32_t nsid, object_id_t object_id, uint32_t page_id)
{
    page_node *new_page = malloc(sizeof(struct page_node));

    new_page->page_id = page_id;
    new_page->object_id = object_id;
    new_page->next = NULL;
    new_page->nsid = nsid;

    HASH_ADD_INT(global_page_table[device_index], page_id, new_page);

    return new_page;
}

page_node *add_page(uint8_t device_index, uint32_t nsid, stored_object *object, uint32_t page_id)
{
    page_node *page = NULL;

    if (lookup_page(device_index, page_id) != NULL)
    {
        RERR(NULL, "The page ID %d already in use.\n", page_id);
    }

    // If this is the first page.
    if (object->pages == NULL)
    {
        object->pages = allocate_new_page(device_index, nsid, object->id, page_id);
        object->size = GET_PAGE_SIZE(device_index);

        return object->pages;
    }

    // Searching for the last page in the linked list.
    for (page = object->pages; page->next != NULL; page = page->next)
    {
        if (page->page_id == page_id)
        {
            RERR(NULL, "[add_page] Object %lu already contains page %d\n", page->object_id, page_id);
        }
    }

    page->next = allocate_new_page(device_index, nsid, object->id, page_id);

    // Increasing the object size by a page.
    object->size += GET_PAGE_SIZE(device_index);

    return page->next;
}

page_node *remove_page(uint8_t device_index, page_node *page)
{
    page_node *next_page = NULL;

    if (page == NULL)
        RERR(NULL, "Failed to remove NULL page.");

    next_page = page->next;

    // Checks if object exist in the hashtable.
    if (page->hh.tbl != NULL)
        HASH_DEL(global_page_table[device_index], page);

    free(page);

    return next_page;
}

page_node *page_by_offset(uint32_t page_size, stored_object *object, unsigned int offset)
{
    page_node *page = NULL;

    // Check if the offset is out of bounds.
    if (offset > object->size)
        return NULL;

    // skim through pages until offset is less than a page's size
    for (page = object->pages; page && offset >= page_size; offset -= page_size, page = page->next)
        ;

    // if page==NULL then page collection < size - report error? or assume it's valid? - this technically shouldn't happen after the if at the beginning. just return NULL if it does
    return page;
}

page_node *lookup_page(uint8_t device_index, uint32_t page_id)
{
    page_node *page = NULL;
    HASH_FIND_INT(global_page_table[device_index], &page_id, page);
    return page;
}
