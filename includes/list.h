#if !defined(_LIST_H)
#define _LIST_H

typedef struct _node {
    void* data;
    struct _node* next;
} node_t;

typedef struct _list {
    node_t* head;
    node_t* tail;
} list_t;

int list_push(list_t* head, void* data);

int list_append(list_t* tail, void* data);

void* list_pop(list_t* head);

int list_destroy(list_t* head, void (*free_fun)(void*));

#endif