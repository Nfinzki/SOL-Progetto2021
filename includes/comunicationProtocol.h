#if !defined(_COMUNICATIONPROTOCOL_H)
#define _COMUNICATIONPROTOCOL_H

#define O_CREATE 01
#define O_LOCK 02

#include <stdlib.h>

int openConnection(const char* sockname, int msec, const struct timespec abstime);


int closeConnection(const char* sockname);


int openFile(const char* pathname, int flags);


int readFile(const char* pathname, void** buf, size_t* size);


/*
* Richiede al server la lettura di ‘N’ files qualsiasi da memorizzare nella directory ‘dirname’ lato client. Se il server
* ha meno di ‘N’ file disponibili, li invia tutti. Se N<=0 la richiesta al server è quella di leggere tutti i file
* memorizzati al suo interno. Ritorna un valore maggiore o uguale a 0 in caso di successo (cioè ritorna il n. di file
* effettivamente letti), -1 in caso di fallimento, errno viene settato opportunamente.
*/
int readNFiles(int N, const char* dirname); //Sono N file qualsiasi, a quanto pare. Non devono essere per forza aperti


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


/*
* Rimuove il file cancellandolo dal file storage server. L’operazione fallisce se il file non è in stato locked, o è in
* stato locked da parte di un processo client diverso da chi effettua la removeFile
*/
int removeFile(const char* pathname);


#endif