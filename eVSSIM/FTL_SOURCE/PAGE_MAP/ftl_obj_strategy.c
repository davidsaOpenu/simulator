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
    obj->size=size;
    //TODO: allocate pages
    return obj;
}

void remove_object(object_id_t object_id)
{
    // STUB
}

page_node* add_page(stored_object *object, int32_t page_id)
{
    // STUB
    return;
}

page_node* page_by_offset(stored_object *object, unsigned int offset)
{
    // STUB
    return;
}

page_node* next_page(stored_object *object,page_node *current)
{
    // STUB
    return;
}
