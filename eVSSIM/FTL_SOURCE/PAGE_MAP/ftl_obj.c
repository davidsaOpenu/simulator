#include <stdlib.h>
#include <search.h>
#include "ftl_obj.h"

#define HASHTABLE_SIZE 100

struct hsearch_data *object_table;
object_id_t current_id;

int init_object_storage() {
    //init hashtable
};

stored_object *lookup_object(object_id_t object_id) {

}

stored_object *create_object(size_t size) {
    object_id_t id = current_id++;
    stored_object *obj = malloc(sizeof(stored_object));
    obj->size=size;
    //TODO: allocate pages
    return obj;
}

void remove_object(object_id_t object_id) {

}

page_node *add_page(stored_object *object, int page_id) {

}
page_node *page_by_offset(stored_object *object, unsigned int offset) {

}
page_node *next_page(stored_object *object,page_node *current) {

}
