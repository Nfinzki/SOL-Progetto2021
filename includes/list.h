#if !defined(_LIST_H)
#define _LIST_H

typedef struct _node {
    void* data;
    struct _node* next;
} node_t;

typedef struct _list {
    node_t* head;
    node_t* tail;
    int dim;
    int (*list_data_compare)(void*, void*);
} list_t;

/* compare function */
int str_compare(void* a, void* b);
int int_compare(void* a, void* b);

int list_create(list_t* lst, int (*compare_fun)(void*, void*));

int list_push(list_t* head, void* data);

int list_append(list_t* tail, void* data);

void* list_pop(list_t* head);

void* list_find(list_t* lst, void* data);

int list_delete(list_t* lst, void* data, void (*free_data)(void*));

int list_destroy(list_t* head, void (*free_fun)(void*));

#endif