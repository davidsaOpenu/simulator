#include "ftl_obj_strategy.h"

#define HASHTABLE_SIZE 100

struct hsearch_data *object_table;
object_id_t current_id;

int init_object_storage()
{
    //init hashtable
    
    // STUB
    return FAIL;
}

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
    // STUB
    return FAIL;
}

int _FTL_OBJ_CREATE(size_t size)
{
    // STUB
    return FAIL;
}

int _FTL_OBJ_DELETE(int32_t object_id)
{
    // STUB
    return FAIL;
}

stored_object* lookup_object(object_id_t object_id)
{
    // STUB
    return;
}

stored_object* create_object(size_t size)
{
    object_id_t id = current_id++;
    stored_object *obj = malloc(sizeof(stored_object));
    int32_t page_id;
    int i,ret;

    obj->size=size;
    for(;size>0;size-=PAGE_SIZE) {
        ret = GET_NEW_PAGE(VICTIM_OVERALL, EMPTY_TABLE_ENTRY_NB,&page_id); // BEN: not sure about these flags
        if(ret==FAIL)
            return NULL;
        add_page(obj,page_id);
    }
    
    //TODO: add obj to hashtable

    return obj;
}

void remove_object(object_id_t object_id)
{
    // STUB
}

page_node *add_page_list(page_node *p, int32_t page_id) {
    if(!p) {
        p = malloc(sizeof(struct page_node));
        p->page_id=page_id;
        p->next=NULL;
    } else if(p->page_id!=page_id) {
        p->next=add_page_list(p->next,page_id);
    }
    return p;
}

page_node *add_page(stored_object *object, int32_t page_id)
{
    object->pages=add_page_list(object->pages,page_id);
}

page_node *page_by_offset(stored_object *object, unsigned int offset)
{
    page_node *page = object->pages;
    if(offset > object->size)
        return NULL; // out of bounds - report error?
    for(;page && offset>0; offset-=PAGE_SIZE, page=page->next)
        ;
    // if page==NULL then page collection < size - report error? or assume it's valid?
    return page;
}

page_node *next_page(stored_object *object,page_node *current)
{
    return current->next;
}
