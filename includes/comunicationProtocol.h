#if !defined(_COMUNICATIONPROTOCOL_H)
#define _COMUNICATIONPROTOCOL_H

#define O_CREATE 01
#define O_LOCK 02

#include <stdlib.h>

typedef struct _openFile {
    char* path;
    int op;
} oFile;

int openConnection(const char* sockname, int msec, const struct timespec abstime);


int closeConnection(const char* sockname);


int openFile(const char* pathname, int flags);


int readFile(const char* pathname, void** buf, size_t* size);


int readNFiles(int N, const char* dirname);


int writeFile(const char* pathname, const char* dirname);


int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);


/*
* In caso di successo setta il flag O_LOCK al file. Se il file era stato aperto/creato con il flag O_LOCK e la
* richiesta proviene dallo stesso processo, oppure se il file non ha il flag O_LOCK settato, l’operazione termina
* immediatamente con successo, altrimenti l’operazione non viene completata fino a quando il flag O_LOCK non
* viene resettato dal detentore della lock. L’ordine di acquisizione della lock sul file non è specificato. Ritorna 0 in
* caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
*/
int lockFile(const char* pathname);


/*
* Resetta il flag O_LOCK sul file ‘pathname’. L’operazione ha successo solo se l’owner della lock è il processo che
* ha richiesto l’operazione, altrimenti l’operazione termina con errore. Ritorna 0 in caso di successo, -1 in caso di
* fallimento, errno viene settato opportunamente.
*/
int unlockFile(const char* pathname);


int closeFile(const char* pathname);


int removeFile(const char* pathname);


#endif