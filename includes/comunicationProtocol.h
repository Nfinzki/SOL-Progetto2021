#if !defined(_COMUNICATIONPROTOCOL_H)
#define _COMUNICATIONPROTOCOL_H

// #define O_CREATE 01
// #define O_LOCK 02

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


int lockFile(const char* pathname);


int unlockFile(const char* pathname);


int closeFile(const char* pathname);


int removeFile(const char* pathname);


#endif