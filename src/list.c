
#include <errno.h>
#include <stdlib.h>

#include "../includes/list.h"

/**
 * Inserisce un elemento in testa alla lista
 * 
 * @param head -- puntatore alla testa della lista
 * @param data -- puntatore all'elemento da inserire in testa
 * 
 * @return 0 on success, -1 on failure and errno is set appropriately
**/
int list_push(list_t* lst, void* data) {
    if (data == NULL) {
        errno = EINVAL;
        return -1;
    }

    node_t *newData = (node_t*)malloc(sizeof(node_t));
    if (newData == NULL) {
        errno = EFAULT; //Ricontrollare il tipo di errore
        return -1;
    }

    newData->data = data;
    newData->next = NULL;

    if (lst->head == NULL) {
        lst->head = newData;
        lst->tail = newData;
    } else {
        newData->next = lst->head;
        lst->head = newData;
    }

    return 0;
}

/**
 * Inserisce un elemento in coda alla lista
 * 
 * @param tail -- puntatore alla coda della lista
 * @param data -- puntatore all'elemento da inserire in coda
 * 
 * @return 0 on success, -1 on failure and errno is set appropriately
**/
int list_append(list_t* lst, void* data) {
    if (data == NULL) {
        errno = EINVAL;
        return -1;
    }

    node_t *newData = (node_t*) malloc(sizeof(node_t));
    if (newData == NULL) {
        errno = EFAULT; //Ricontrollare il tipo di errore
        return -1;
    }

    newData->data = data;
    newData->next = NULL;

    if (lst->tail == NULL) {
        lst->head = newData;
        lst->tail = newData;
    } else {
        (lst->tail)->next = newData;
    }

    return 0;
}

/**
 * Rimuove l'elemento dalla testa della lista
 * 
 * @param head -- puntatore alla testa della lista
 * 
 * @return il puntatore all'elemento rimosso dalla lista. Altrimenti restituisce NULL
**/
void* list_pop(list_t* lst) {
    if (lst->head == NULL) {
        errno = EINVAL;
        return NULL;
    }

    node_t *oldHead = lst->head;
    lst->head = (lst->head)->next;

    if (lst->head == NULL) lst->tail = NULL;

    return oldHead->data;
}

/**
 * Libera la memoria di tutta la lista
 * 
 * @param head -- puntatore alla testa della lista
 * @param free_fun -- puntatore alla funzione per liberare un elemento della lista
 * 
 * @return 0 on success, -1 on failure and errno is set appropriately
**/
int list_destroy(list_t* lst, void (*free_fun)(void*)) {
    if (lst->head == NULL || (*free_fun) == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    node_t *curr;

    while(lst->head != NULL) {
        curr = lst->head;
        lst->head = (lst->head)->next;

        (*free_fun)(curr);
    }
    lst->tail = NULL;

    return 0;
}