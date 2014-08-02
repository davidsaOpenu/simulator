#ifndef _FTL_OBJ_H_
#define _FTL_OBJ_H_

typedef unsigned long object_id_t;

typedef struct {
    object_id_t object_id;
    size_t size;
    page_node *pages;
} stored_object;

typedef struct page_node {
    int page_id;
    struct page_node *next;
} page_node;

int init_object_storage();

stored_object lookup_object(object_id_t object_id);
object_id_t create_object(size_t size);
void remove_object(object_id_t object_id);

page_node *add_page(stored_object *object, int page_id);
page_node *page_by_offset(stored_object *object, unsigned int offset);
page_node *next_page(stored_object *object,page_node *current);

#endif
