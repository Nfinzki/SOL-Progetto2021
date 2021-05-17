#if !defined(_UTIL_H)
#define _UTIL_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define SYSCALL_NOT_ZERO_EXIT(r, f, name)    \
    if ((r = f) != 0) {             \
        errno = r;                  \
        perror(name);               \
        exit(errno);                \
    }

#define SYSCALL_NOT_ONE_EXIT(f, name)    \
    if (f != -1) {             \
        perror(name);               \
        exit(errno);                \
    }

/*
Controlla se la stringa s è un numero ed eventualmente
la stringa convertita viene messa in n.
Restituisce: 0 in caso di successo
             1 se non è un numero 
             2 in caso di overflow/underflow
*/
static inline int isNumber(const char* s, long* n) {
  if (s == NULL) return 1;
  if (strlen(s) == 0) return 1;
  char* e = NULL;
  errno = 0;
  long val = strtol(s, &e, 10);
  if (errno == ERANGE) return 2;    // overflow/underflow
  if (e != NULL && *e == (char)0) {
    *n = val;
    return 0;   // successo 
  }
  return 1;   // non e' un numero
}

static inline void Pthread_mutex_lock(pthread_mutex_t *mtx) {
    int err;
    if ((err = pthread_mutex_lock(mtx)) != 0) {
        errno = err;
        perror("lock");
        pthread_exit((void*) errno);
    }
}

static inline void Pthread_mutex_unlock(pthread_mutex_t *mtx) {
    int err;
    if ((err = pthread_mutex_unlock(mtx)) != 0) {
        errno = err;
        perror("unlock");
        pthread_exit((void*) errno);
    }
}

#endif