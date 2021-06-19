/**
 * @file list.c
 * 
 * @author Francesco Di Luzio
**/

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "../includes/list.h"


int str_compare(void* a, void* b) {
    return (strcmp( (char*)a, (char*)b ) == 0);
}

int int_compare(void* a, void* b) {
    return (*(int*)a == *(int*)b);
}

/**
 * Crea la lista
 * 
 * @param lst -- puntatore alla lista
 * @param compare_fun -- puntatore alla funzione che compara due elementi della lista
 * 
 * @return puntatore alla lista in caso di successo, NULL in caso di errore (sets errno)
**/
list_t* list_create(list_t* lst, int (*compare_fun)(void*, void*)) {
    if (!*compare_fun) {
        errno = EINVAL;
        return NULL;
    }

    lst = malloc(sizeof(list_t));
    if (lst == NULL) {errno = ENOMEM; return NULL;}
    lst->head = NULL;
    lst->tail = NULL;
    lst->dim = 0;
    lst->list_data_compare = compare_fun;

    return lst;
}


/**
 * Inserisce un elemento in testa alla lista
 * 
 * @param lst -- puntatore alla lista
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
        errno = ENOMEM;
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

    lst->dim++;

    return 0;
}


/**
 * Inserisce un elemento in coda alla lista
 * 
 * @param lst -- puntatore alla lista
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
        errno = ENOMEM;
        return -1;
    }

    newData->data = data;
    newData->next = NULL;

    if (lst->tail == NULL) {
        lst->head = newData;
        lst->tail = newData;
    } else {
        (lst->tail)->next = newData;
        lst->tail = newData;
    }

    lst->dim++;

    return 0;
}


/**
 * Rimuove l'elemento dalla testa della lista
 * 
 * @param lst -- puntatore alla lista
 * 
 * @return il puntatore all'elemento rimosso dalla lista. Altrimenti restituisce NULL
**/
void* list_pop(list_t* lst) {
    errno = 0;
    if (lst->head == NULL) {
        errno = EINVAL;
        return NULL;
    }

    node_t *oldHead = lst->head;
    lst->head = (lst->head)->next;

    if (lst->head == NULL) lst->tail = NULL;

    lst->dim--;

    void* result = oldHead->data;
    free(oldHead);

    return result;
}


/** 
 * Cerca l'elemento all'interno della lista
 * 
 * @param lst -- puntatore alla lista
 * @param data -- puntatore all'elemento da cercare
 * 
 * @return puntatore all'elemento che corrisponde a data. Se l'elemento non è stato trovato restituisce NULL
**/
void* list_find(list_t* lst, void* data) {
    if (lst == NULL || data == NULL) return NULL;

    node_t *curr = lst->head;

    while(curr != NULL) {
        if (lst->list_data_compare(curr->data, data)) return curr->data;
        curr = curr->next;
    }

    return NULL;
}


/**
 * Restituisce il puntatore al prossimo elemento della lista. In chiamate successive non va passato
 * il puntatore alla lista ma solo lo stato
 * 
 * @param lst -- puntatore all lista
 * @param state -- stato della getNext
 * 
 * @return puntatore al prossimo elemento della lista. Altrimenti restituisce NULL
*/
void* list_getNext(list_t* lst, node_t** state) {
    if (lst == NULL && *state == NULL) return NULL;

    if (lst != NULL) {
        *state = lst->head;
        return lst->head->data;
    }

    if (state != NULL) {
        *state = (*state)->next;
        if (*state != NULL) return (*state)->data;
    }
    
    return NULL;
}


/**
 * Cancella un elemento dalla lista
 * 
 * @param lst -- puntatore alla lista
 * @param elem -- puntatore all'elemento da rimuovere
 * @param free_data -- puntatore alla funzione per liberare un elemento della lista
 * 
 * @return 0 on success, -1 on failure and errno is set appropriately 
**/
int list_delete(list_t* lst, void* data, void (*free_data)(void*)) {
    if (lst == NULL || data == NULL) {
        errno = EINVAL;
        return -1;
    }

    node_t *prev = NULL;
    node_t *curr = lst->head;

    while(curr != NULL) {
        if (lst->list_data_compare(curr->data, data)) {
            if (prev == NULL) {
                lst->head = curr->next;
                if (lst->head == NULL) lst->tail = NULL;
            } else {
                prev->next = curr->next;
                if (lst->list_data_compare(curr->data, lst->tail->data)) lst->tail = prev;
            }
            
            if (*free_data && curr->data) (*free_data)(curr->data);
            lst->dim--;
            free(curr);
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    errno = EINVAL;
    return -1;
}


/**
 * Libera la memoria di tutta la lista
 * 
 * @param lst -- puntatore alla lista
 * @param free_fun -- puntatore alla funzione per liberare un elemento della lista
 * 
 * @return 0 on success, -1 on failure and errno is set appropriately
**/
int list_destroy(list_t* lst, void (*free_fun)(void*)) {
    if ((*free_fun) == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    node_t *curr;

    while(lst->head != NULL) {
        curr = lst->head;
        lst->head = (lst->head)->next;

        (*free_fun)(curr->data);
        free(curr);
    }
    lst->tail = NULL;
    lst->dim = 0;

    free(lst);
    return 0;
}