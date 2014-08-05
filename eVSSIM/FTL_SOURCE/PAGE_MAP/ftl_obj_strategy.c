#include "ftl_obj_strategy.h"

#define WRAP_SUCCESS_FAIL(x) return (x != NULL) ? SUCCESS : FAIL;

stored_object *objects_table = NULL;
object_id_t current_id;

int _FTL_OBJ_READ(int32_t object_id, unsigned int offset, unsigned int length)
{
    // STUB
    return FAIL;
}

int _FTL_OBJ_WRITE(int32_t object_id, unsigned int offset, unsigned int length)
{
    // STUB
    return FAIL;
}

int _FTL_OBJ_COPYBACK(int32_t source, int32_t destination)
{
    int32_t lpn; //The logical page address, the page that being moved.
    page_node *source_p;
    
    source_p = lookup_page(source);
    
    // source_p can be NULL if the GC is working on some old pages that belonged to an object we deleted already
    if (source_p != NULL)
    {
        // change the object's page mapping to the new page
        source_p->page_id = destination;
    }
#ifdef FTL_DEBUG
    else
    {
        printf("Warning[%s] %u copyback page not mapped to an object \n", __FUNCTION__, source);
    }
#endif
    
    //Handle page map
	lpn = GET_INVERSE_MAPPING_INFO(source);
	if (lpn != -1)
	{
		//The given physical page is being map, the mapping information need to be changed
		UPDATE_OLD_PAGE_MAPPING(lpn); //as far as i can tell when being called under the gc manage all the actions are being done, but what if will be called from another place?
		UPDATE_NEW_PAGE_MAPPING(lpn, destination);
	}

    return SUCCESS;
}

int _FTL_OBJ_CREATE(size_t size)
{
    stored_object *new_object;
    
    new_object = create_object(size);
    
    if (new_object == NULL)
        return FAIL;
    
    // need to return the new id so the os will know how to talk to us about it
    return new_object->id;
}

int _FTL_OBJ_DELETE(int32_t object_id)
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
    obj->size = size;
    for(; size > 0; size -= PAGE_SIZE)
    {
        if (GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB, &page_id) == FAIL)
        {
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
    
    // remove obj from hashtable only if it exists there because it could just be cleanup in case create_object failed
    if (lookup_object(object->id) != NULL)
        HASH_DEL(objects_table, object);
    
    current_page = object->pages;
    while (current_page != NULL)
    {
        // invalidate the physical page and update its mapping
        UPDATE_OLD_PHYSICAL_PAGE_MAPPING(current_page->page_id);
        
#ifdef GC_ON
        // should we really perform GC for every page? we know we are invalidating a lot of them now...
        GC_CHECK(CALC_FLASH(current_page->page_id), CALC_BLOCK(current_page->page_id));
#endif

        // get next page and free the current one
        invalidated_page = current_page;
        current_page = current_page->next;
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
    return object->pages = add_page_list(object->pages,page_id);
}

page_node *page_by_offset(stored_object *object, unsigned int offset)
{
    if(offset > object->size)
        return NULL; // out of bounds - report error? - returning NULL should count as error, no?
    
    struct page_node *page = object->pages;
    
    // skim through pages until offset is less than a page's size
    for(;page && offset>=PAGE_SIZE; offset-=PAGE_SIZE, page=page->next)
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
        for (page = obj->pages; page != NULL; page = page->next)
        {
            if (page->page_id == page_id)
                return page;
        }
    }
    
    // failed to find the page in any of our objects
    return NULL;
}

page_node *next_page(stored_object *object,page_node *current)
{
    return current->next;
}
