#include "ftl_obj_strategy.h"

#define WRAP_SUCCESS_FAIL(x) return (x != NULL) ? SUCCESS : FAIL;

stored_object *objects_table = NULL;
object_id_t current_id;

void INIT_OBJ_STRATEGY()
{
    current_id = 1; //0 marks failure...
}

int _FTL_OBJ_READ(object_id_t object_id, unsigned int offset, unsigned int length)
{
    stored_object *object;
    page_node *current_page;
    int io_page_nb;
    int curr_io_page_nb;
    unsigned int ret = FAIL;
    
    object = lookup_object(object_id);
    
    // file not found
    if (object == NULL)
        return FAIL;
    // object not big enough
    if (object->size < (offset *  + length))
        return FAIL;
    
    current_page = page_by_offset(object, offset);
    
    // just calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna read
	io_alloc_overhead = ALLOC_IO_REQUEST(current_page->page_id, length, READ, &io_page_nb);
    
    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        // simulate the page read
        ret = SSD_PAGE_READ(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), CALC_PAGE(current_page->page_id), curr_io_page_nb, READ, io_page_nb);
        
		// send a physical read action being done to the statistics gathering
		if (ret == SUCCESS)
		{
			FTL_STATISTICS_GATHERING(current_page->page_id, PHYSICAL_READ);
		}

#ifdef FTL_DEBUG
		if (ret == FAIL)
		{
			printf("Error[%s] %u page read fail \n", __FUNCTION__, current_page->page_id);
		}
#endif
        
        // get the next page
        current_page = next_page(object, current_page);
    }
    
    INCREASE_IO_REQUEST_SEQ_NB();

#ifdef MONITOR_ON
	char szTemp[1024];
	sprintf(szTemp, "READ PAGE %d ", length);
	WRITE_LOG(szTemp);
#endif

#ifdef FTL_DEBUG
	printf("[%s] Complete\n",__FUNCTION__);
#endif

	return ret;
}

int _FTL_OBJ_WRITE(object_id_t object_id, unsigned int offset, unsigned int length)
{
    stored_object *object;
    page_node *current_page = NULL;
    int32_t page_id;
    int io_page_nb;
    int curr_io_page_nb;
    unsigned int ret = FAIL;
    
    object = lookup_object(object_id);
    
    // file not found
    if (object == NULL)
        return FAIL;
    
    // calculate the overhead of allocating the request. io_page_nb will be the total number of pages we're gonna write
    io_alloc_overhead = ALLOC_IO_REQUEST(offset, length, WRITE, &io_page_nb);
    
    // if the offset is past the current size of the stored_object we need to append new pages until we can start writing
    while ((offset - PAGE_SIZE) > object->size)
    {
        if (GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &page_id) == FAIL)
        {
            // not enough memory presumably
            printf("ERROR[FTL_WRITE] Get new page fail \n");
            return FAIL;
        }
        
        add_page(object, page_id);
    }

    for (curr_io_page_nb = 0; curr_io_page_nb < io_page_nb; curr_io_page_nb++)
    {
        // if this is the first iteration we need to find the page by offset, otherwise we can go with the page chain
        if (current_page == NULL)
            current_page = page_by_offset(object, offset);
        else
            current_page = next_page(object, current_page);
        
        // get the pge we'll be writing to
        if (GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &page_id) == FAIL)
        {
            printf("ERROR[FTL_WRITE] Get new page fail \n");
            return FAIL;
        }
        
        if (current_page == NULL) // writing at the end of the object and need to allocate more space for it
        {
            current_page = add_page(object, page_id);
        }
        else // writing over parts of the object
        {
            // invalidate the old physical page and replace the page_node's page
            UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), CALC_PAGE(current_page->page_id), INVALID);
            current_page->page_id = page_id;
        }
        
        ret = SSD_PAGE_WRITE(CALC_FLASH(page_id), CALC_BLOCK(page_id), CALC_PAGE(page_id), curr_io_page_nb, WRITE, io_page_nb);
        
		// send a physical write action being done to the statistics gathering
		if (ret == SUCCESS)
		{
			FTL_STATISTICS_GATHERING(page_id , PHYSICAL_WRITE);
		}
        
#ifdef GC_ON
        // must improve this because it is very possible that we will do multiple GCs on the same flash chip and block
        // probably gonna add an array to hold the unique ones and in the end GC all of them
        GC_CHECK(CALC_FLASH(page_id), CALC_BLOCK(page_id));
#endif
        
#ifdef FTL_DEBUG
        if (ret == FAIL)
        {
            printf("Error[FTL_WRITE] %d page write fail \n", page_id);
        }
#endif
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
	printf("[%s] Complete\n",__FUNCTION__);
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
        
        // change the object's page mapping to the new page
        source_p->page_id = destination;
    }
#ifdef FTL_DEBUG
    else
    {
        printf("Warning[%s] %u copyback page not mapped to an object \n", __FUNCTION__, source);
    }
#endif

    return SUCCESS;
}

int _FTL_OBJ_CREATE(size_t size)
{
    stored_object *new_object;
    
    new_object = create_object(size);
    
    if (new_object == NULL) {
        printf("_FTL_OBJ_CREATE\n");
        return FAIL;
    }
    
    // need to return the new id so the os will know how to talk to us about it
    return new_object->id;
}

int _FTL_OBJ_DELETE(object_id_t object_id)
{
    stored_object *object;
    
    object = lookup_object(object_id);
    
    // object not found
    if (object == NULL)
        return FAIL;

    return remove_object(object);
}

stored_object *lookup_object(object_id_t object_id)
{
    stored_object *object;
    
    // try to find it in our hashtable. NULL will be returned if key not found
    HASH_FIND_INT(objects_table, &object_id, object);
    
    return object;
}

stored_object *create_object(size_t size)
{
    stored_object *obj = malloc(sizeof(stored_object));
    int32_t page_id;

    // initialize to stored_object struct with size and initial pages
    obj->id = current_id++;
    obj->size = 0;
    obj->pages = NULL;

    while(size > obj->size)
    {
        if (GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &page_id) == FAIL)
        {
            printf("create_object");
            // cleanup just in case we managed to do anything up until now
            remove_object(obj);
            return NULL;
        }
        
        add_page(obj, page_id);
    }
    
    // add the new object to the objects' hashtable
    HASH_ADD_INT(objects_table, id, obj); 

    return obj;
}

int remove_object(stored_object *object)
{
    page_node *current_page;
    page_node *invalidated_page;
    
    // object could not exist in the hashtable yet because it could just be cleanup in case create_object failed
    // if we do perform HASH_DEL on an object that is not in the hashtable, the whole hashtable will be deleted
    if (object->hh.tbl != NULL)
        HASH_DEL(objects_table, object);
    
    current_page = object->pages;
    while (current_page != NULL)
    {
        // invalidate the physical page and update its mapping
        UPDATE_INVERSE_BLOCK_VALIDITY(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id), CALC_PAGE(current_page->page_id), INVALID);
        
#ifdef GC_ON
        // should we really perform GC for every page? we know we are invalidating a lot of them now...
        GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id));
#endif

        // get next page and free the current one
        invalidated_page = current_page;
        current_page = next_page(object, current_page);
        free(invalidated_page);
    }
    
    // free the object's memory
    free(object);
    
    return SUCCESS;
}

page_node *add_page_list(page_node *p, int32_t page_id) {
    if(!p)
    {
        p = malloc(sizeof(struct page_node));
        p->page_id = page_id;
        p->next = NULL;
    }
    else if (p->page_id != page_id)
    {
        p->next = add_page_list(p->next, page_id);
    }
    
    //what should we do if p->page_id==page_id?
    return p;
}

page_node *add_page(stored_object *object, int32_t page_id)
{
    object->size += PAGE_SIZE;
    return object->pages = add_page_list(object->pages,page_id);
}

page_node *page_by_offset(stored_object *object, unsigned int offset)
{
    page_node *page;
    
    // check if out of bounds
    if(offset > object->size)
        return NULL;
    
    // skim through pages until offset is less than a page's size
    for(page = object->pages; page && offset >= PAGE_SIZE; offset -= PAGE_SIZE, page = next_page(object, page))
        ;
    
    // if page==NULL then page collection < size - report error? or assume it's valid? - this technically shouldn't happen after the if at the beginning. just return NULL if it does
    return page;
}

page_node *lookup_page(int32_t page_id)
{
    stored_object *obj;
    page_node *page;

    for (obj = objects_table; obj != NULL; obj = obj->hh.next)
    {
        for (page = obj->pages; page != NULL; page = next_page(obj, page))
        {
            if (page->page_id == page_id)
                return page;
        }
    }
    
    // failed to find the page in any of our objects
    return NULL;
}

page_node *next_page(stored_object *object, page_node *current)
{
    return current->next;
}
