#include "ftl_obj_strategy.h"

#include "osd.h"
#include "osd-util/osd-util.h"
#include "osd-util/osd-defs.h"

#ifndef COMPLIANCE_TESTS
#include "hw.h"
#endif

uint8_t *osd_sense;
static const uint32_t cdb_cont_len = 0;
struct osd_device osd;

stored_object *objects_table = NULL;
object_map *objects_mapping = NULL;
partition_map *partitions_mapping = NULL;
page_node *global_page_table = NULL;
object_id_t current_id;


void INIT_OBJ_STRATEGY(void)
{
    current_id = 1;
    objects_table = NULL;
    objects_mapping = NULL;
    partitions_mapping = NULL;
    global_page_table = NULL;
}

void free_obj_table(void)
{
    stored_object *curr,*next;
    for (curr = objects_table; curr; curr=next) {
        next=curr->hh.next;
        free(curr);
    }
}

void free_obj_mapping(void)
{
	object_map *curr,*next;
    for (curr = objects_mapping; curr; curr=next) {
        next=curr->hh.next;
        free(curr);
    }
}

void free_part_mapping(void)
{
	partition_map *curr,*next;
    for (curr = partitions_mapping; curr; curr=next) {
        next=curr->hh.next;
        free(curr);
    }
}

void free_page_table(void)
{
    page_node *curr,*next;
    for (curr = global_page_table; curr; curr=next) {
        next=curr->next;
        free(curr);
    }
}

void TERM_OBJ_STRATEGY(void)
{
    free_obj_table();
    free_obj_mapping();
    free_part_mapping();
    free_page_table();
}

int _FTL_OBJ_READ(object_id_t object_id, unsigned int offset, unsigned int length)
{
    stored_object *object;
    page_node *current_page;
    int io_page_nb;
    int curr_io_page_nb;
    unsigned int ret = FAILURE;
    
    object = lookup_object(object_id);
    
    // file not found
    if (object == NULL)
        return FAILURE;
    // object not big enough
    if (object->size < (offset + length))
        return FAILURE;
    
    if(!(current_page = page_by_offset(object, offset)))
    {
        LOG_VSSIMDBG("Error[%s] %u lookup page by offset failed \n", __FUNCTION__, current_page->page_id);
        return FAILURE;
    }
    
    // just calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna read
	io_alloc_overhead = ALLOC_IO_REQUEST(current_page->page_id * SECTORS_PER_PAGE, length, READ, &io_page_nb);
    
    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        // simulate the page read
        ret = SSD_PAGE_READ(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), CALC_PAGE(current_page->page_id), curr_io_page_nb, READ, io_page_nb);
        
		// send a physical read action being done to the statistics gathering
		if (ret == SUCCESSFUL)
		{
			FTL_STATISTICS_GATHERING(current_page->page_id, PHYSICAL_READ);
		}

#ifdef FTL_DEBUG
		if (ret == FAILURE)
		{
			LOG_VSSIMDBG("Error[%s] %u page read fail \n", __FUNCTION__, current_page->page_id);
		}
#endif
        
        // get the next page
        current_page = current_page->next;
    }
    
    INCREASE_IO_REQUEST_SEQ_NB();

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "READ PAGE %d ", length);
	WRITE_LOG(szTemp);
#endif

#ifdef FTL_DEBUG
	LOG_VSSIMDBG("[%s] Complete\n",__FUNCTION__);
#endif

	return ret;
}

int _FTL_OBJ_WRITE(object_id_t object_id, unsigned int offset, unsigned int length)
{
    stored_object *object;
    page_node *current_page = NULL,*temp_page;
    uint32_t page_id;
    int io_page_nb;
    int curr_io_page_nb;
    unsigned int ret = FAILURE;
    
    object = lookup_object(object_id);
    
    // file not found
    if (object == NULL)
    {
    	LOG_VSSIMDBG("[%s] failed lookup\n", __FUNCTION__);
        return FAILURE;
    }
    
    // calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna write
    io_alloc_overhead = ALLOC_IO_REQUEST(offset, length, WRITE, &io_page_nb);
    
    // if the offset is past the current size of the stored_object we need to append new pages until we can start writing
    while (offset > object->size)
    {
        if (GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &page_id) == FAILURE)
        {
            // not enough memory presumably
            LOG_VSSIMDBG("ERROR[FTL_WRITE] Get new page fail \n");
            return FAILURE;
        }
        if(!add_page(object, page_id))
            return FAILURE;
        
        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(page_id);
    }

    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        // if this is the first iteration we need to find the page by offset, otherwise we can go with the page chain
        if (current_page == NULL)
            current_page = page_by_offset(object, offset);
        else
            current_page = current_page->next;
        
        // get the pge we'll be writing to
        if (GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &page_id) == FAILURE)
        {
            LOG_VSSIMDBG("ERROR[FTL_WRITE] Get new page fail \n");
            return FAILURE;
        }
        if((temp_page=lookup_page(page_id)))
        {
            LOG_VSSIMDBG("ERROR[FTL_WRITE] Object %lu already contains page %d\n",temp_page->object_id,page_id);
            return FAILURE;
        }
        
        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(page_id);
        
        if (current_page == NULL) // writing at the end of the object and need to allocate more space for it
        {
            current_page = add_page(object, page_id);
            if(!current_page)
                return FAILURE;
        }
        else // writing over parts of the object
        {
            // invalidate the old physical page and replace the page_node's page
            UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), CALC_PAGE(current_page->page_id), INVALID);
            UPDATE_INVERSE_PAGE_MAPPING(current_page->page_id, -1);            

            HASH_DEL(global_page_table, current_page); 
            current_page->page_id = page_id;
            HASH_ADD_INT(global_page_table, page_id, current_page); 
        }
#ifdef GC_ON
            // must improve this because it is very possible that we will do multiple GCs on the same flash chip and block
            // probably gonna add an array to hold the unique ones and in the end GC all of them
            GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), false, true);
#endif
        
        ret = SSD_PAGE_WRITE(CALC_FLASH(page_id), CALC_BLOCK(page_id), CALC_PAGE(page_id), curr_io_page_nb, WRITE, io_page_nb);
        
		// send a physical write action being done to the statistics gathering
		if (ret == SUCCESSFUL)
		{
			FTL_STATISTICS_GATHERING(page_id , PHYSICAL_WRITE);
		}
        
#ifdef FTL_DEBUG
        if (ret == FAILURE)
        {
            LOG_VSSIMDBG("Error[FTL_WRITE] %d page write fail \n", page_id);
        }
#endif

//        page_node *page;
//        printf("Object page map:{");
//        for(page=object->pages; page; page=page->next)
//            printf("%d->",page->page_id);
//        printf("}\n");

    }

    INCREASE_IO_REQUEST_SEQ_NB();

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "WRITE PAGE %d ", length);
	WRITE_LOG(szTemp);
	sprintf(szTemp, "WB CORRECT %d", curr_io_page_nb);
	WRITE_LOG(szTemp);
#endif

#ifdef FTL_DEBUG
	LOG_VSSIMDBG("[%s] Complete\n",__FUNCTION__);
#endif

	return ret;
}

int _FTL_OBJ_COPYBACK(int32_t source, int32_t destination)
{
    page_node *source_p;
    
    source_p = lookup_page(source);
    
    // source_p can be NULL if the GC is working on some old pages that belonged to an object we deleted already
    if (source_p != NULL)
    {
        // invalidate the source page
        UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(source), CALC_BLOCK(source), CALC_PAGE(source), INVALID);
        
        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(destination);
        
        // change the object's page mapping to the new page
        HASH_DEL(global_page_table, source_p); 
        source_p->page_id = destination;
        HASH_ADD_INT(global_page_table, page_id, source_p); 
    }
#ifdef FTL_DEBUG
    else
    {
        LOG_VSSIMDBG("Warning[%s] %u copyback page not mapped to an object \n", __FUNCTION__, source);
    }
#endif

    return SUCCESSFUL;
}

bool _FTL_OBJ_CREATE(object_id_t obj_id, size_t size)
{
    stored_object *new_object;
    
    new_object = create_object(obj_id, size);
    
    if (new_object == NULL) {
        return false;
    }
    
    return true;
}

int _FTL_OBJ_DELETE(object_id_t object_id)
{
    stored_object *object;
    object_map *obj_map;
    
    object = lookup_object(object_id);
    
    // object not found
    if (object == NULL)
    	return FAILURE;

    obj_map = lookup_object_mapping(object_id);

    // object_map not found
    if (obj_map == NULL)
    	return FAILURE;

    return remove_object(object, obj_map);
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

stored_object *create_object(object_id_t obj_id, size_t size)
{

    stored_object *obj = malloc(sizeof(stored_object));
    uint32_t page_id;

    object_map *obj_map;

    HASH_FIND_INT(objects_mapping, &obj_id, obj_map);
    //if the requested id was not found, let's add it
    if (obj_map == NULL)
    {
    	object_map *new_obj_id = (object_map*)malloc(sizeof(object_map));
    	new_obj_id->id = obj_id;
    	new_obj_id->exists = true;
    	HASH_ADD_INT(objects_mapping, id, new_obj_id);
    }
    else
    {
    	LOG_VSSIMDBG("[%s] Object %lu already exists, cannot create it !\n", __FUNCTION__, obj_id);
    	return NULL;
    }


    // initialize to stored_object struct with size and initial pages
    obj->id = obj_id;
    obj->size = 0;
    obj->pages = NULL;

    // add the new object to the objects' hashtable
    HASH_ADD_INT(objects_table, id, obj); 

    while(size > obj->size)
    {
        if (GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &page_id) == FAILURE)
        {
            // cleanup just in case we managed to do anything up until now
            remove_object(obj, obj_map);
            return NULL;
        }

        if(!add_page(obj, page_id))
            return NULL;
        
        // mark new page as valid and used
        UPDATE_NEW_PAGE_MAPPING_NO_LOGICAL(page_id);
    }

    return obj;
}

int remove_object(stored_object *object, object_map *obj_map)
{
    page_node *current_page;
    page_node *invalidated_page;
    
    // object could not exist in the hashtable yet because it could just be cleanup in case create_object failed
    // if we do perform HASH_DEL on an object that is not in the hashtable, the whole hashtable will be deleted
    if (object->hh.tbl != NULL)
        HASH_DEL(objects_table, object);
    
    if (obj_map->hh.tbl != NULL)
    	HASH_DEL(objects_mapping, obj_map);

    current_page = object->pages;
    while (current_page != NULL)
    {
        // invalidate the physical page and update its mapping
        UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), CALC_PAGE(current_page->page_id), INVALID);
        
#ifdef GC_ON
        // should we really perform GC for every page? we know we are invalidating a lot of them now...
        GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), true, true);
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
    
    return SUCCESSFUL;
}

page_node *allocate_new_page(object_id_t object_id, uint32_t page_id)
{
    page_node *page = malloc(sizeof(struct page_node));
    page->page_id = page_id;
    page->object_id = object_id;
    page->next = NULL;
    return page;
}

page_node *add_page(stored_object *object, uint32_t page_id)
{
    page_node *curr,*page;

    HASH_FIND_INT(global_page_table,&page_id,page);
    if(page)
    {
        LOG_VSSIMDBG("ERROR[add_page] Object %lu already contains page %d\n",page->object_id,page_id);
        return NULL;
    }

    object->size += eVSSIM_PAGE_SIZE;
    if(object->pages == NULL)
    {
        page = allocate_new_page(object->id,page_id);
        HASH_ADD_INT(global_page_table, page_id, page); 
        object->pages = page;
        return object->pages;
    }
    for(curr=page=object->pages; page && page->page_id != page_id; curr=page,page=page->next)
        ;
    if(page)
    {
        LOG_VSSIMDBG("ERROR[add_page] Object %lu already contains page %d\n",page->object_id,page_id);
        return NULL;
    }

    page = allocate_new_page(object->id,page_id);
    HASH_ADD_INT(global_page_table, page_id, page); 
    curr->next = page;
    return curr->next;
}

 void _FTL_OBJ_WRITECREATE(object_location obj_loc, size_t size)
{

	//If that's the first prp, we need to create the object
	if (obj_loc.create_object)
	{
		LOG_VSSIMDBG("[%s] about to create an object in the SIMULATOR -> obj id: %lu size: %lu\n", __FUNCTION__, obj_loc.object_id, size);
		bool created = _FTL_OBJ_CREATE(obj_loc.object_id, size);
		LOG_VSSIMDBG("[%s] created the SIMULATOR object !\n", __FUNCTION__);

		if (!created)
		{
			LOG_VSSIMDBG("[%s] could not create the SIMULATOR object. Aborting !\n", __FUNCTION__);
			return;
		}
	}

	LOG_VSSIMDBG("[%s] about to write an object to the SIMULATOR -> obj id: %lu size: %lu\n", __FUNCTION__, obj_loc.object_id, size);
	_FTL_OBJ_WRITE(obj_loc.object_id, 0, size);
	//LOG_VSSIMDBG("[%s] object written to the SIMULATOR with res:%d\n", __FUNCTION__, res);

	return;
}

 bool osd_init(void) {
	 const char *rm_command = "rm -rf ";
	 char *rm_tmp_osd_command = malloc(strlen(OSD_PATH)+strlen(rm_command)+1);

	 if (rm_tmp_osd_command != NULL)
	 {
		 rm_tmp_osd_command[0] = '\0';
		 strcat(rm_tmp_osd_command, rm_command);
		 strcat(rm_tmp_osd_command, OSD_PATH);
	 }
	 else
	 {
		 LOG_VSSIMDBG("malloc failed !\n");
		 return false;
	 }

	 if (system(rm_tmp_osd_command))
		 return false;
	 if (osd_open(OSD_PATH, &osd))
		 return false;
	 LOG_VSSIMDBG("[%s] osd_init() finished successfully !\n", __FUNCTION__);
	 return true;

 }

 bool create_partition(partition_id_t part_id)
 {

	 const char *root = "/tmp/osd/";
	 if (osd_open(root, &osd))
		 return false;
	 osd_sense = (uint8_t*)Calloc(1, 1024);
	 if (osd_create_partition(&osd, part_id, cdb_cont_len, osd_sense))
		 return false;
	 LOG_VSSIMDBG("[%s] created partition: %lu finished successfully !\n", __FUNCTION__, part_id);
	 return true;
 }

void OSD_WRITE_OBJ(object_location obj_loc, unsigned int length, uint8_t *buf)
{
	int ret;
	partition_id_t part_id = USEROBJECT_PID_LB + obj_loc.partition_id;
	object_id_t obj_id = USEROBJECT_OID_LB + obj_loc.object_id;

	if (obj_loc.create_object)
	{
	    partition_map *part_map;

	    HASH_FIND_INT(partitions_mapping, &part_id, part_map);
	    //if the requested id was not found, let's add it
	    if (part_map == NULL)
	    {
	    	LOG_VSSIMDBG("[%s] osd partition %lu does not exist - trying to create it!\n", __FUNCTION__, part_id);
	    	bool created = create_partition(part_id);
	    	if (created)
			{
	    		partition_map *new_partition_id = (partition_map*)malloc(sizeof(partition_map));
				new_partition_id->id = part_id;
				new_partition_id->exists = true;
				HASH_ADD_INT(partitions_mapping, id, new_partition_id);
		    	LOG_VSSIMDBG("[%s] osd partition %lu created successfully!\n", __FUNCTION__, part_id);

			}
	    	else
	    	{
	    		LOG_VSSIMDBG("[%s] Could not create an osd partition !\n", __FUNCTION__);
	    		return;
	    	}

	    }
	    else
	    {
	    	LOG_VSSIMDBG("partition %lu already exists, no need to create it !\n", part_id);
	    }

		LOG_VSSIMDBG("[%s] creating an writing object to OSD with partition id: %lu and object id: %lu of size: %d\n", __FUNCTION__, part_id, obj_id, length);
		ret = osd_create_and_write(&osd, part_id, obj_id, length, 0, buf, cdb_cont_len, 0, osd_sense, DDT_CONTIG);
		if (ret) {
			LOG_VSSIMDBG("[%s] FAILED ! ret for osd_create_and_write() is: %d\n", __FUNCTION__, ret);
			return;
		}
		LOG_VSSIMDBG("[%s] object was created and written to osd !\n", __FUNCTION__);
	}

	else{
		LOG_VSSIMDBG("[%s] updating OSD object id: %lu of size: %d\n", __FUNCTION__, obj_id, length);
		ret = osd_append(&osd,part_id, obj_id, length, buf, cdb_cont_len, osd_sense, DDT_CONTIG);
		if (ret) {
			LOG_VSSIMDBG("[%s] FAIL! ret for osd_append() is: %d\n", __FUNCTION__, ret);

			return;
		}
		LOG_VSSIMDBG("[%s] OSD object was updated successfully\n", __FUNCTION__);
	}
}

void printMemoryDump(uint8_t *buffer, unsigned int bufferLength)
{
	unsigned int* start_p = (unsigned int*)buffer;
	unsigned int* end_p = (unsigned int*)buffer + bufferLength / sizeof(unsigned int);
	LOG_VSSIMDBG("code dump length: %ld\n", end_p - start_p);
	int i =0,iall = 0;
	char msgBuf[512];
	char* msg = (char*)msgBuf;
	int gotSomething = 0;
	LOG_VSSIMDBG("---------------------------------------\nDUMP MEMORY START\n0x%016lX - 0x%016lX\n---------------------------------------\n",(uint64_t)start_p,(uint64_t)end_p);

	while (start_p < end_p){
		char* asCharArray = (char*)start_p;
		if (*start_p != 0){
			gotSomething = 1;
		}

		if (i == 0){
			msg += sprintf(msg, "#%d: 0x%016lX | ", iall, (uint64_t)start_p);
		}
		msg += sprintf(msg, "0x%X %c%c%c%c | ", *start_p, asCharArray[0], asCharArray[1], asCharArray[2], asCharArray[3]);
		if (i == 3) {
			if (gotSomething){
				LOG_VSSIMDBG("%s\n",msgBuf);
			}
			i = 0;
			gotSomething = 0;
			msg = (char*)msgBuf;
		}else{
			msg += sprintf(msg, " ");
			i++;
		}
		start_p++;
		iall++;

	}
	LOG_VSSIMDBG("---------------------------------------\nDUMP MEMORY END\n---------------------------------------\n");
}

void OSD_READ_OBJ(object_location obj_loc, unsigned int length, uint64_t addr, uint64_t offset)
{
	object_id_t obj_id = USEROBJECT_OID_LB + obj_loc.object_id;
	partition_id_t part_id = USEROBJECT_PID_LB + obj_loc.partition_id;


	LOG_VSSIMDBG("[%s] READING %d bytes from OSD OBJECT: %lu %lu\n", __FUNCTION__, length, part_id, obj_id);
	uint64_t len;

	uint8_t *rdbuf = malloc(length);

	//we should also get the offset here, for cases where there's more than one prp

	if(osd_read(&osd, part_id, obj_id, length, offset, NULL, rdbuf, &len, 0, osd_sense, DDT_CONTIG))
		LOG_VSSIMDBG("[%s] failed in osd_read()\n", __FUNCTION__);
	else
	{
		LOG_VSSIMDBG("[%s] osd_read() was successful. %lu bytes were read !\n", __FUNCTION__, len);

#ifndef COMPLIANCE_TESTS
		cpu_physical_memory_rw(addr, rdbuf, len, 1);
#endif

		printMemoryDump(rdbuf, length);

	}
	free(rdbuf);
}

page_node *page_by_offset(stored_object *object, unsigned int offset)
{
    page_node *page;
    
    // check if out of bounds
    if(offset > object->size)
        return NULL;
    
    // skim through pages until offset is less than a page's size
    for(page = object->pages; page && offset >= eVSSIM_PAGE_SIZE; offset -= eVSSIM_PAGE_SIZE, page = page->next)
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
