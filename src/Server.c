#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>

#include "../includes/util.h"
#include "../includes/comunication.h"
#include "../includes/list.h"
#include "../includes/comunicationOptions.h"
#include "../includes/comunicationFlags.h"

#include "../includes/icl_hash.h"

#define STRLEN 256
#define UNIX_PATH_MAX 108
#define MAXCONN 100
#define BUCKETS 250

typedef struct _filestorage {
    icl_hash_t *files;
    long max_space;
    int max_file;
    long actual_space;
    int actual_numFile;
    int max_file_reached;
    long max_space_reached;
    int replacementPolicy; //Numero di volte in cui è entrato in funzione l'algoritmo di sostituzione
} file_storage_t;

file_storage_t* fileStorage;
static pthread_mutex_t mutex_storage = PTHREAD_MUTEX_INITIALIZER;

list_t* fileHistory;
static pthread_mutex_t mutex_filehistory = PTHREAD_MUTEX_INITIALIZER;

typedef struct _file_t {
    char* path;
    size_t byteDim;
    void* data;
    int M;
    list_t* clients; //Lista dei file che hanno aperto il file
    int isLocked; //Lock mutex
    pthread_mutex_t mutex_file;
    pthread_cond_t wait_lock;
    int lock; //Lock logico che può richiedere il client
    list_t* pendingLock; //Lista dei client che attendono l'acquisizione del lock
    int isDeleted;
} file_t;

list_t* connection;
static pthread_mutex_t mutex_connections = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_emptyConnections = PTHREAD_COND_INITIALIZER;

int sigCaught = 0;
int stopRequests = 0;
int stopConnections = 0;

char* socketName;
char* logFile;

//Set delle connessioni ancora attive
fd_set setConnected;
int connectedMax = 0;
static pthread_mutex_t mutex_fdset = PTHREAD_MUTEX_INITIALIZER;

file_storage_t* initializeStorage(file_storage_t* storage) {
    if ((storage = malloc(sizeof(file_storage_t))) == NULL) return NULL;

    if ((storage->files = icl_hash_create(BUCKETS, hash_pjw, string_compare)) == NULL) {errno = ECANCELED; return NULL;}
    storage->max_space = 0;
    storage->max_file = 0;
    storage->actual_space = 0;
    storage->actual_numFile = 0;
    storage->max_file_reached = 0;
    storage->max_space_reached = 0;
    storage->replacementPolicy = 0;

    return storage;
}

//Libera la memoria di un file
void freeFile(void* f) {
    file_t *file = (file_t*) f;
    
    free(file->path);
    free(file->data);
    SYSCALL_ONE_EXIT(list_destroy(file->pendingLock, free), "list_destroy")
    SYSCALL_ONE_EXIT(list_destroy(file->clients, free), "list_destroy")
    free(file);
}

int FIFO_ReplacementPolicy(long space, int numFiles, int fd, char* pathInvoked) {
    int pathInv = 0;

    Pthread_mutex_lock(&mutex_storage);
    Pthread_mutex_lock(&mutex_filehistory);
    fileStorage->replacementPolicy++;

    while((space > fileStorage->max_space || numFiles > fileStorage->max_file) && !pathInv) {
        char* oldFile_name = (char*) list_pop(fileHistory);

        if (oldFile_name == NULL) {
            Pthread_mutex_unlock(&mutex_filehistory);
            Pthread_mutex_unlock(&mutex_storage);
            return -1;
        }

        if (!pathInv)
            pathInv = str_compare(oldFile_name, pathInvoked);

        file_t* oldFile = (file_t*) icl_hash_find(fileStorage->files, oldFile_name);
        if (oldFile == NULL) { //Inconsistenza, il file storage non è più affidabile
            fprintf(stderr, "Inconsistenza tra lo storage e la cronologia dei file\n");
            exit(EXIT_FAILURE);
        }

        //Notifica i worker in attesa di acquisire la lock che il file è cancellato
        Pthread_mutex_lock(&(oldFile->mutex_file));
        oldFile->isLocked = 1;
        oldFile->isDeleted = 1;
        pthread_cond_broadcast(&(oldFile->wait_lock));
        Pthread_mutex_unlock(&(oldFile->mutex_file));

        //Invia esito negativo a tutti i client in attesa di acquisire la lock
        int *fd_waiting;
        int res = ENOENT;
        while ((fd_waiting = list_pop(oldFile->pendingLock)) != NULL) {
            SYSCALL_ONE_RETURN_F(writen(*fd_waiting, &res, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
            free(fd_waiting);
        }

        //Invia il file vittima al client se è stato modificato
        if (fd != -1 && oldFile->M) {
            int opt = SEND_FILE;
            SYSCALL_ONE_RETURN_F(writen(fd, &opt, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
            int len = strnlen(oldFile->path, STRLEN);
            SYSCALL_ONE_RETURN_F(writen(fd, &len, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
            SYSCALL_ONE_RETURN_F(writen(fd, oldFile->path, len * sizeof(char)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
            SYSCALL_ONE_RETURN_F(writen(fd, &(oldFile->byteDim), sizeof(size_t)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
            SYSCALL_ONE_RETURN_F(writen(fd, oldFile->data, oldFile->byteDim * sizeof(char)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        }

        //Aggiorna le informazioni del file storage
        space -= oldFile->byteDim;
        numFiles--;
        fileStorage->actual_space -= oldFile->byteDim;
        fileStorage->actual_numFile--;

        //Elimina il file dallo storage
        if (icl_hash_delete(fileStorage->files, oldFile_name, NULL, freeFile) == -1) exit(EXIT_FAILURE);
    }
    Pthread_mutex_unlock(&mutex_filehistory);
    Pthread_mutex_unlock(&mutex_storage);

    return pathInv;
}

void freeGlobal(){
    if (socketName != NULL) free(socketName);
    if (logFile != NULL) free(logFile);
}

//Gestione dei segnali
void* sighandler(void* arg){
    int sigPipe = *(int*) arg;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGHUP);

    int sig;
    int err;
    SYSCALL_NOT_ZERO_EXIT(err, sigwait(&mask, &sig), "sigwait")

    switch (sig) {
        case SIGINT:
        case SIGQUIT: stopRequests = 1;
        case SIGHUP: stopConnections = 1; break;
    }

    close(sigPipe);

    return NULL;
}

void setHandlers() {
    //Maschera i segnali SIGINT, SIGQUIT, SIGHUP che verranno gestiti dal thread handler
    int err;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGHUP);
    SYSCALL_NOT_ZERO_EXIT(err, pthread_sigmask(SIG_BLOCK, &mask, NULL), "pthread_sigmask")

    //Ignoro SIGPIPE
    struct sigaction s;
    memset(&s, 0, sizeof(s));
    s.sa_handler = SIG_IGN;
    SYSCALL_ONE_EXIT(sigaction(SIGPIPE, &s, NULL), "sigaction")
}

//Effettua il parsing del file di configurazione
//Restituisce 0 in caso di successo, -1 in caso di fallimento
int parseFile(const char* filepath, int* numWorkers, long* memorySpace, int* numFile, char** sockName, char** logFile) {
    FILE *fd;
    if((fd = fopen(filepath, "r")) == NULL) {
        perror("fopen");
        exit(errno);
    }

    char* str = calloc(STRLEN, sizeof(char));
    EQ_NULL_RETURN(str, "calloc")

    while(fgets(str, STRLEN, fd) != NULL) {
        if (str[strnlen(str, STRLEN) - 1] == '\n') str[strnlen(str, STRLEN) - 1] = '\0';

        if (strncmp(str, "N_WORKERS", 9) == 0){
            long num;
            int err;
            if ((err = isNumber(str + 10, &num)) != 0) {
                if (err == 1)
                    fprintf(stderr, "isNumber: %s not a number\n", str + 10);
                else
                    fprintf(stderr, "isNumber: overflow or underflow\n");
                free(str);
                return -1;
            }

            *numWorkers = num;
        }

        if(strncmp(str, "MEM_SPACE", 9) == 0){ //Non sono ammessi spazi tra la quantità e l'unità di misura
            int K = 0;
            int M = 0;
            int G = 0;
            int B = 0;

            int len = strnlen(str, STRLEN) + 1;
            for(int i = 10; i < len; i++) {
                if (str[i] == 'K' || str[i] == 'k') {
                    K = 1;
                    str[i] = '\0';
                }
                if (str[i] == 'M' || str[i] == 'm') {
                    M = 1;
                    str[i] = '\0';
                }
                if (str[i] == 'G' || str[i] == 'g') {
                    G = 1;
                    str[i] = '\0';
                }
                if (str[i] == 'B' || str[i] == 'b') B = 1;
                if (str[i] == ' ') str[i] = '\0';
            }

            if (!B) {
                fprintf(stderr, "Wrong format for memory capacity\n");
                free(str);
                return -1;
            }

            int err;
            if ((err = isNumber(str + 10, memorySpace)) != 0) {
                if (err == 1)
                    fprintf(stderr, "isNumber: %s not a number\n", str + 10);
                else
                    fprintf(stderr, "isNumber: overflow or underflow\n");
                free(str);
                return -1;
            }

            if (K) *memorySpace = *memorySpace * 1024;
            if (M) *memorySpace = *memorySpace * 1024 * 1024;
            if (G) *memorySpace = *memorySpace * 1024 * 1024 * 1024;
        }

        if(strncmp(str, "FILE_SPACE", 10) == 0) {
            long num;
            int err;
            if ((err = isNumber(str + 11, &num)) != 0) {
                if (err == 1)
                    fprintf(stderr, "isNumber: %s not a number\n", str + 10);
                else
                    fprintf(stderr, "isNumber: overflow or underflow\n");
                free(str);
                return -1;
            }

            *numFile = num;
        }

        if(strncmp(str, "SOCKET_NAME", 11) == 0) {
            int len = strnlen(str + 12, STRLEN) + 1; //Sfrutto il fatto che conta \n per allocare lo spazio del \0
            *sockName = calloc(len, sizeof(char));
            if (*sockName == NULL) {
                perror("calloc");
                free(str);
                return -1;
            }
            
            strncpy(*sockName, str + 12, len);
        }

        if(strncmp(str, "LOG_FILE", 8) == 0) {
            int len = strnlen(str + 9, STRLEN) + 1; //Sfrutto il fatto che conta \n per allocare lo spazio del \0
            *logFile = calloc(len, sizeof(char));
            if (*logFile == NULL) {
                perror("calloc");
                free(str);
                return -1;
            }
            
            strncpy(*logFile, str + 9, len);
        }

        memset(str, 0, sizeof(char) * STRLEN);
    }

    if(!feof(fd)) {
        fprintf(stderr, "An error has occurred reading %s\n", filepath);
        return -1;
    }

    free(str);
    fclose(fd);
    return 0;
}

//Controlla se il detentore della lock è ancora connesso. Se così non fosse assegna la lock al prossimo richiedente
void checkLock(file_t *f) {
    int res;
    Pthread_mutex_lock(&mutex_fdset);

    if (!FD_ISSET(f->lock, &setConnected)) {
        int *lock_fd = list_pop(f->pendingLock);

        if (lock_fd == NULL) {
            f->lock = 0;
        } else {
            f->lock = *lock_fd;
            free(lock_fd);
            res = 0;
            writen(f->lock, &res, sizeof(int));
        }
    }

    Pthread_mutex_unlock(&mutex_fdset);
}

//Crea un nuovo file all'interno del file storage
int createFile(int fd, int len, char* path) {
    int res;
    file_t *newFile = malloc(sizeof(file_t));
    EQ_NULL_EXIT(newFile, "malloc in createFile")

    int err;

    //Inizializzazione nuovo file
    newFile->byteDim = 0;
    newFile->data = NULL;
    newFile->M = 0;
    newFile->lock = 0;
    SYSCALL_NOT_ZERO_RETURN_F(err, pthread_mutex_init(&(newFile->mutex_file), NULL), "pthread_mutex_init", free(newFile); writen(fd, &res, sizeof(int)))
    newFile->isLocked = 0;
    newFile->isDeleted = 0;
    SYSCALL_NOT_ZERO_RETURN_F(err, pthread_cond_init(&(newFile->wait_lock), NULL), "pthread_cond_init", free(newFile); writen(fd, &res, sizeof(int)))
    EQ_NULL_EXIT(newFile->pendingLock =  list_create(newFile->pendingLock, int_compare), "list_create") //list_create ritorna NULL solo se la malloc all'interno fallisce

    newFile->path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(newFile->path, "calloc in createFile")

    strncpy(newFile->path, path, len);

    EQ_NULL_EXIT(newFile->clients =  list_create(newFile->clients, int_compare), "list_create")

    //Controlla se il nuovo file viola i vincoli del file storage 
    if (fileStorage->actual_space + newFile->byteDim > fileStorage->max_space || fileStorage->actual_numFile + 1 > fileStorage->max_file) {
        Pthread_mutex_unlock(&mutex_storage);

        int resPolicy;
        if ((resPolicy = FIFO_ReplacementPolicy(fileStorage->actual_space + newFile->byteDim, fileStorage->actual_numFile + 1, -1, newFile->path)) != 0) {
            if (resPolicy == -1) {
                fprintf(stderr, "Errore nell'algoritmo di esecuzione\n");
                res = ENOMEM;
            } else {
                res = ECANCELED;
            }
            free(newFile->path);
            list_destroy(newFile->clients, free);
            list_destroy(newFile->pendingLock, free);
            free(newFile);
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }

        Pthread_mutex_lock(&mutex_storage);
    }

    //Aggiunge il nuovo file al file storage
    if (icl_hash_insert(fileStorage->files, newFile->path, newFile) == NULL) {
        fprintf(stderr, "Errore in icl_hash_insert\n");
        free(newFile->path);
        list_destroy(newFile->clients, free);
        list_destroy(newFile->pendingLock, free);
        free(newFile);
        res = ECANCELED;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    //Aggiorna le informazioni del file storage
    fileStorage->actual_space += newFile->byteDim;
    fileStorage->actual_numFile++;
    if (fileStorage->actual_space > fileStorage->max_space_reached) fileStorage->max_space_reached = fileStorage->actual_space;
    if (fileStorage->actual_numFile > fileStorage->max_file_reached) fileStorage->max_file_reached = fileStorage->actual_numFile;

    Pthread_mutex_unlock(&mutex_storage);

    //Aggiunge il path del nuovo file in coda a fileHistory
    Pthread_mutex_lock(&mutex_filehistory);
    if (list_append(fileHistory, newFile->path) == -1) {
        perror("list_append");
        icl_hash_delete(fileStorage->files, newFile->path, NULL, freeFile);
        res = ECANCELED;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }
    Pthread_mutex_unlock(&mutex_filehistory);

    return 0;
}

//Verifica se il file è presente nel file storage
int findFile(int fd) {
    int len;
    int found;
    int res;

    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in findFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    if(icl_hash_find(fileStorage->files, path) == NULL)
        found = 0;
    else
        found = 1;
    Pthread_mutex_unlock(&mutex_storage);
    
    free(path);

    SYSCALL_ONE_RETURN(writen(fd, &found, sizeof(int)), "writen")

    return 0;
}

//Apre un file, con la possibilità di crearne uno e lockarlo
int openFile(int fd) {
    int len;
    int res;
    int flag;

    //Lettura dei flag
    SYSCALL_ONE_RETURN_F(readn(fd, &flag, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))
    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in openFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    if ((flag & O_CREATE) == O_CREATE) { //Flag O_CREATE specificato, crea il file
        res = createFile(fd, len, path);
        if (res == -1) return -1;
    }

    //Ricerca del file nel file storage
    file_t *f = (file_t*) icl_hash_find(fileStorage->files, path);
    free(path);
    if (f == NULL) { //File non presente all'interno dello storage
        Pthread_mutex_unlock(&mutex_storage);
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }
    Pthread_mutex_lock(&(f->mutex_file));
    Pthread_mutex_unlock(&mutex_storage);

    while(f->isLocked && !f->isDeleted)
        pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));
    
    //Controlla se il file è stato cancellato
    if (f->isDeleted) {
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    f->isLocked = 1;
    Pthread_mutex_unlock(&(f->mutex_file));

    //Se il file è già aperto restituisce esito positivo al client. Non effettua cambiamenti
    if (list_find(f->clients, &fd) != NULL) {
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));

        res = 0;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return 0;
    }
    
    int *newClient = malloc(sizeof(int));
    EQ_NULL_EXIT(newClient, "malloc error in openFile")

    *newClient = fd;

    //Aggiunge il client nella lista dei client che hanno aperto il file
    if(list_push(f->clients, newClient) == -1) {
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));

        res = ECANCELED;
        free(newClient);
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    if ((flag & O_LOCK) == O_LOCK){ //Flag O_LOCK specificato, verifica disponibilità
        if (f->lock != fd && f->lock != 0) { //File lockato ma non dal client
            checkLock(f);

            if (f->lock != 0) { //File lockato, il client viene messo in attesa
                int *lock_fd = malloc(sizeof(int));
                EQ_NULL_EXIT(lock_fd, "malloc in lock_file")

                *lock_fd = fd;
                SYSCALL_ONE_RETURN_F(list_append(f->pendingLock, lock_fd), "list_append in lock_file", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = ECANCELED; writen(fd, &res, sizeof(int)))
                
                Pthread_mutex_lock(&(f->mutex_file));
                f->isLocked = 0;
                pthread_cond_signal(&(f->wait_lock));
                Pthread_mutex_unlock(&(f->mutex_file));

                return 0;
            }
        }

        if (f->lock == 0) f->lock = fd; //Il client acquisisce la lock
    }

    //Rilascia la mutua esclusione
    Pthread_mutex_lock(&(f->mutex_file));
    f->isLocked = 0;
    pthread_cond_signal(&(f->wait_lock));
    Pthread_mutex_unlock(&(f->mutex_file));

    //Invia l'esito al client
    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
    
    return 0;
}

//Chiude la connessione con un client
int closeConnection(int fd) {
    int numFiles;
    int res;

    //Lettura del numero di file da chiudere
    SYSCALL_ONE_RETURN_F(readn(fd, &numFiles, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    for(int i = 0; i < numFiles; i++) {
        int len;
        //Lettura della lunghezza del path
        SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))
        
        char* path = calloc(len, sizeof(char));
        EQ_NULL_EXIT(path, "calloc in closeConnection")

        //Lettura del path
        SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

        Pthread_mutex_lock(&mutex_storage);

        //Ricerca del file nel file storage
        file_t *f = (file_t*) icl_hash_find(fileStorage->files, path);
        free(path);
        if (f == NULL) { //File non presente nello storage
            Pthread_mutex_unlock(&mutex_storage);
            res = ENOENT;
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }
        Pthread_mutex_lock(&(f->mutex_file));
        Pthread_mutex_unlock(&mutex_storage);

        while(f->isLocked && !f->isDeleted)
            pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));

        //Controlla se il file è stato cancellato
        if (f->isDeleted) {
            res = ENOENT;
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }

        f->isLocked = 1;
        Pthread_mutex_unlock(&(f->mutex_file));

        //Se il client aveva la lock sul file viene fatta la unlock ed eventualmente si assegna la lock ad un altro client in attesa
        if (f->lock != 0) {
            int *lock_fd = list_pop(f->pendingLock);

            if (lock_fd == NULL) {
                f->lock = 0;
            } else {
                f->lock = *lock_fd;
                free(lock_fd);
                res = 0;
                //Invia il risultato al client in attesa di acquisire la lock
                SYSCALL_ONE_RETURN_F(writen(f->lock, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; pthread_cond_signal(&(f->wait_lock)); Pthread_mutex_unlock(&(f->mutex_file)))
            }
        }

        //Elimina il client dalla lista dei client che hanno aperto il file
        SYSCALL_ONE_RETURN_F(list_delete(f->clients, &fd, free), "list_delete in closeConnection", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = ECANCELED; writen(fd, &res, sizeof(int)))
        
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        pthread_cond_signal(&(f->wait_lock));
        Pthread_mutex_unlock(&(f->mutex_file));
    }

    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

//Invia un file al client richiedente
int readFile(int fd) {
    int len;
    int res;
    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in readFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    //Ricerca del file all'interno del file storage
    file_t *f = (file_t*) icl_hash_find(fileStorage->files, path);
    free(path);
    if (f == NULL) { //File non presente nello storage
        Pthread_mutex_unlock(&mutex_storage);
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }
    Pthread_mutex_lock(&(f->mutex_file));
    Pthread_mutex_unlock(&mutex_storage);

    while(f->isLocked && !f->isDeleted)
        pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));

    //Controlla se il file è stato cancellato
    if (f->isDeleted) {
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    f->isLocked = 1;
    Pthread_mutex_unlock(&(f->mutex_file));

    //Controlla che il client abbia aperto il file
    if (list_find(f->clients, &fd) == NULL) {
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));

        res = EACCES;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    //Controlla se il file è lockato da qualcun altro
    if (f->lock != fd && f->lock != 0) {
        checkLock(f);

        if (f->lock != 0) {
            Pthread_mutex_lock(&(f->mutex_file));
            f->isLocked = 0;
            Pthread_mutex_unlock(&(f->mutex_file));

            res = EPERM;
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }
    }

    //Comunica al Client che non ci sono stati errori a recuperare il file
    res = 0;
    SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)))

    //Invia al client la dimensione e il contenuto del file
    SYSCALL_ONE_RETURN_F(writen(fd, &(f->byteDim), sizeof(size_t)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)))
    SYSCALL_ONE_RETURN_F(writen(fd, f->data, f->byteDim * sizeof(char)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)))
    
    Pthread_mutex_lock(&(f->mutex_file));
    f->isLocked = 0;
    pthread_cond_signal(&(f->wait_lock));
    Pthread_mutex_unlock(&(f->mutex_file));

    return 0;
}

//Invia al client che ne fa richiesta N file.
//Eventuali lock sui file vengono ignorate
int readnFile(int fd) {
    int N;
    int res;
    //Lettura del numero di file da inviare al client
    SYSCALL_ONE_RETURN_F(readn(fd, &N, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    if (N <= 0 || N > fileStorage->actual_numFile) N = fileStorage->actual_numFile;
    
    if (N == 0) { //Non sono presenti file nello storage
        Pthread_mutex_unlock(&mutex_storage);
        res = 0;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return 0;
    }

    Pthread_mutex_lock(&mutex_filehistory);
    //Legge un path alla volta da fileHistory
    node_t* state = NULL;
    char* path = (char*) list_getNext(fileHistory, &state);
    int i = 0;
    while(i < N && path != NULL) {
        //Cerca il file nello storage
        file_t *f = icl_hash_find(fileStorage->files, path);
        if (f == NULL) {
            fprintf(stderr, "Errore critico: Inconsistenza tra storage e fileHistory\n");
            exit(EXIT_FAILURE);
        }

        Pthread_mutex_lock(&(f->mutex_file));

        while(f->isLocked && !f->isDeleted)
            pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));

        //Controlla se il file è stato cancellato
        if (f->isDeleted) {
            res = ENOENT;
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }

        f->isLocked = 1;
        Pthread_mutex_unlock(&(f->mutex_file));

        //Invia il file al client
        int opt = SEND_FILE;
        SYSCALL_ONE_RETURN_F(writen(fd, &opt, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        int len = strnlen(path, STRLEN) + 1;
        SYSCALL_ONE_RETURN_F(writen(fd, &len, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        SYSCALL_ONE_RETURN_F(writen(fd, path, len * sizeof(char)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        SYSCALL_ONE_RETURN_F(writen(fd, &(f->byteDim), sizeof(size_t)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        SYSCALL_ONE_RETURN_F(writen(fd, f->data, f->byteDim * sizeof(char)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))

        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        pthread_cond_signal(&(f->wait_lock));
        Pthread_mutex_unlock(&(f->mutex_file));

        i++;

        //Richiede il prossimo path da inviare
        path = (char*) list_getNext(NULL, &state);
    } 
    Pthread_mutex_unlock(&mutex_filehistory);
    Pthread_mutex_unlock(&mutex_storage);

    //Comunica al client che i file da ricevere sono terminati
    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

//Effettua la scrittura su un file
int writeFile(int fd) {
    int len;
    int res;

    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in writeFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    //Ricerca del file all'interno del file storage
    file_t *f = (file_t*) icl_hash_find(fileStorage->files, path);
    free(path);
    if (f == NULL) { //File non presente nello storage
        Pthread_mutex_unlock(&mutex_storage);
        fprintf(stderr, "File non presente nello storage\n");
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    Pthread_mutex_lock(&(f->mutex_file));
    Pthread_mutex_unlock(&mutex_storage);

    while(f->isLocked && !f->isDeleted)
        pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));

    //Controlla se il file è stato cancellato
    if (f->isDeleted) {
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    f->isLocked = 1;
    Pthread_mutex_unlock(&(f->mutex_file));

    //Controlla che il client abbia aperto quel file
    if (list_find(f->clients, &fd) == NULL) {
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));
        fprintf(stderr, "File non aperto dal client\n");
        res = EACCES;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    //Controlla se il file è lockato da qualcun altro
    if (f->lock != fd && f->lock != 0) {
        checkLock(f);

        if (f->lock != 0) {
            Pthread_mutex_lock(&(f->mutex_file));
            f->isLocked = 0;
            Pthread_mutex_unlock(&(f->mutex_file));
            res = EPERM;
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }
    }

    //Lettura della dimensione del buffer da leggere
    size_t dim;
    SYSCALL_ONE_RETURN_F(readn(fd, &dim, sizeof(size_t)), "readn", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = EINTR; writen(fd, &res, sizeof(int)))

    //Lettura del buffer da scrivere sul file 'dim' byte alla volta
    while(dim != 0) {
        char* buf = calloc(dim, sizeof(char));
        EQ_NULL_EXIT(buf, "calloc in writeFile")
        SYSCALL_ONE_RETURN_F(readn(fd, buf, dim * sizeof(char)), "readn", free(buf); Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = EINTR; writen(fd, &res, sizeof(int)))

        //Se la quantità da scrivere è più grande della capacità massima dello storage non potrà mai essere inserito nel server
        //Oppure se la quantità complessiva del file supera la capacità massima dello storage elimina tutto quello che è stato scritto fino ad ora
        if (dim > fileStorage->max_space || f->byteDim + dim > fileStorage->max_space) {
            Pthread_mutex_lock(&(f->mutex_file));
            f->isLocked = 0;
            Pthread_mutex_unlock(&(f->mutex_file));

            if (f->data != NULL) free(f->data);
            free(buf);
            res = EFBIG;
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }

        //Controlla se vengono superati i limiti del file storage
        if (fileStorage->actual_space + dim > fileStorage->max_space) {
            int resPolicy;
            if ((resPolicy = FIFO_ReplacementPolicy(fileStorage->actual_space + dim, fileStorage->actual_numFile, fd, f->path)) != 0) {
                if (resPolicy == -1) {
                    fprintf(stderr, "Errore nell'esecuzione dell'algoritmo di sostituzione\n");
                    res = ENOMEM;
                } else { //È stato rimosso il file da scrivere
                    res = ECANCELED;
                }

                Pthread_mutex_lock(&(f->mutex_file));
                f->isLocked = 0;
                Pthread_mutex_unlock(&(f->mutex_file));
                free(buf);
                SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
                return -1;
            }
        }

        //Aggiunge il buffer letto al file
        if (f->data == NULL) {
            f->data = buf;
        } else {
            char* tmp = realloc(f->data, (f->byteDim + dim) * sizeof(char));
            EQ_NULL_EXIT_F(tmp, "realloc in appendFile", free(buf);)
        
            f->data = tmp;
            memcpy((char*)f->data + f->byteDim, buf, dim);
            free(buf);
        }
        f->byteDim += dim;

        //Aggiorna le dimensioni dello storage
        Pthread_mutex_lock(&mutex_storage);
        fileStorage->actual_space += dim;
        if (fileStorage->actual_space > fileStorage->max_space_reached) fileStorage->max_space_reached = fileStorage->actual_space;
        Pthread_mutex_unlock(&mutex_storage);

        //Lettura della lunghezza del prossimo buffer
        SYSCALL_ONE_RETURN_F(readn(fd, &dim, sizeof(size_t)), "readn", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = EINTR; writen(fd, &res, sizeof(int)))
    }
    f->M = 1;
    Pthread_mutex_lock(&(f->mutex_file));
    f->isLocked = 0;
    pthread_cond_signal(&(f->wait_lock));
    Pthread_mutex_unlock(&(f->mutex_file));

    //Invia esito positivo al client
    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

//Scrive in append su un file
int appendFile(int fd) {
    int len;
    int res;

    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in appendFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    //Ricerca del file all'interno del file storage
    file_t *f = (file_t*) icl_hash_find(fileStorage->files, path);
    free(path);
    if (f == NULL) { //File non presente all'interno dello storage
        Pthread_mutex_unlock(&mutex_storage);
        fprintf(stderr, "File non presente nello storage\n");
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }
    Pthread_mutex_lock(&(f->mutex_file));
    Pthread_mutex_unlock(&mutex_storage);

    while(f->isLocked && !f->isDeleted)
        pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));

    //Controlla se il file è stato cancellato
    if (f->isDeleted) {
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    f->isLocked = 1;
    Pthread_mutex_unlock(&(f->mutex_file));
    
    //Controlla che il client abbia aperto quel file
    if (list_find(f->clients, &fd) == NULL) {
        fprintf(stderr, "File non aperto dal client\n");
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));
        res = EACCES;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    //Controlla se il file è lockato da qualcun altro
    if (f->lock != fd && f->lock != 0) {
        checkLock(f);

        if (f->lock != 0) {
            Pthread_mutex_lock(&(f->mutex_file));
            f->isLocked = 0;
            Pthread_mutex_unlock(&(f->mutex_file));
            res = EPERM;
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }
    }
    
    //Lettura della dimensione del buffer
    size_t fileDim;
    SYSCALL_ONE_RETURN_F(readn(fd, &fileDim, sizeof(size_t)), "readn", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = EINTR; writen(fd, &res, sizeof(int)))

    char* newData = calloc(fileDim, sizeof(char));
    EQ_NULL_EXIT(newData, "malloc")

    //Lettura del buffer
    SYSCALL_ONE_RETURN_F(readn(fd, newData, fileDim * sizeof(char)), "readn", free(newData); Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = EINTR; writen(fd, &res, sizeof(int)))

    //Se la quantità da scrivere è più grande della capacità massima dello storage non potrà mai essere inserito nel server
    //Oppure se la quantità complessiva del file supera la capacità massima dello storage non inserisce i nuovi dati nel file
    if (fileDim > fileStorage->max_space || f->byteDim + fileDim > fileStorage->max_space) {
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));
        free(newData);
        res = EFBIG;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    //Controlla se vengono superati i limiti del file storage
    if (fileStorage->actual_space + fileDim > fileStorage->max_space) {
        int resPolicy;
        if ((resPolicy = FIFO_ReplacementPolicy(fileStorage->actual_space + fileDim, fileStorage->actual_numFile, fd, f->path)) != 0) {
            if (resPolicy == -1) {
                fprintf(stderr, "Errore nell'esecuzione dell'algoritmo di sostituzione\n");
                res = ENOMEM;
            } else { //È stato designato come vittima il file attualmente in uso
                res = ECANCELED;
            }
            Pthread_mutex_lock(&(f->mutex_file));
            f->isLocked = 0;
            Pthread_mutex_unlock(&(f->mutex_file));

            free(newData);
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }
    }

    if (f->data == NULL)
        f->data = newData;
    else {
        char* tmp = realloc(f->data, (f->byteDim + fileDim) * sizeof(char));
        EQ_NULL_EXIT(tmp, "realloc in appendFile")
        
        f->data = tmp;
        memcpy((char*)f->data + f->byteDim, newData, fileDim);
        free(newData);
    }
    f->byteDim += fileDim;
    f->M = 1;
    Pthread_mutex_lock(&(f->mutex_file));
    f->isLocked = 0;
    pthread_cond_signal(&(f->wait_lock));
    Pthread_mutex_unlock(&(f->mutex_file));
    
    //Aggiorna le dimensioni dello storage
    Pthread_mutex_lock(&mutex_storage);
    fileStorage->actual_space += fileDim;
    if (fileStorage->actual_space > fileStorage->max_space_reached) fileStorage->max_space_reached = fileStorage->actual_space;
    Pthread_mutex_unlock(&mutex_storage);

    //Invia esito positivo al client
    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

//Chiude un file per un client
int closeFile(int fd) {
    int len;
    int res;
    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in closeFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    //Ricerca del file all'interno del file storage
    file_t *file = (file_t*) icl_hash_find(fileStorage->files, path);
    free(path);

    if (file == NULL) { //File non presente nello storage
        Pthread_mutex_unlock(&mutex_storage);
        fprintf(stderr, "File non presente nello storage\n");
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }
    Pthread_mutex_lock(&(file->mutex_file));
    Pthread_mutex_unlock(&mutex_storage);

    while(file->isLocked && !file->isDeleted)
        pthread_cond_wait(&(file->wait_lock), &(file->mutex_file));

    //Controlla se il file è stato cancellato
    if (file->isDeleted) {
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    file->isLocked = 1;
    Pthread_mutex_unlock(&(file->mutex_file));

    //Se il client aveva la lock sul file si unlocka ed eventualmente si assegna ad un altro client
    if (file->lock == fd) {
        int *lock_fd = list_pop(file->pendingLock);
        if (lock_fd == NULL) {
            file->lock = 0;
        } else {
            file->lock = *lock_fd;
            free(lock_fd);
            res = 0;
            SYSCALL_ONE_RETURN_F(writen(file->lock, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(file->mutex_file)); file->isLocked = 0; Pthread_mutex_unlock(&(file->mutex_file)))
        }
    }

    //Elimina il client dalla lista dei client che hanno aperto il file
    if (list_delete(file->clients, &fd, free) == -1) {
        res = EINVAL;
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(file->mutex_file)); file->isLocked = 0; Pthread_mutex_unlock(&(file->mutex_file)))
        return -1;
    }
    
    Pthread_mutex_lock(&(file->mutex_file));
    file->isLocked = 0;
    pthread_cond_signal(&(file->wait_lock));
    Pthread_mutex_unlock(&(file->mutex_file));

    //Invia esito positivo al client
    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

//Rimuove un file dal server
int removeFile(int fd) {
    int len;
    int res;
    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in removeFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    Pthread_mutex_lock(&mutex_filehistory);
    //Ricerca il file all'interno del file storage
    file_t *file = (file_t*) icl_hash_find(fileStorage->files, path);
    if (file == NULL) { //File non presente nello storage
        Pthread_mutex_unlock(&mutex_filehistory);
        Pthread_mutex_unlock(&mutex_storage);

        fprintf(stderr, "File non presente nello storage\n");
        free(path);
        res = ENOENT;
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        return -1;
    }

    Pthread_mutex_lock(&(file->mutex_file));

    while(file->isLocked && !file->isDeleted)
        pthread_cond_wait(&(file->wait_lock), &(file->mutex_file));

    //Controlla se il file è stato cancellato
    if (file->isDeleted) {
        free(path);
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    file->isLocked = 1;
    Pthread_mutex_unlock(&(file->mutex_file));

    if (file->lock != fd) { //Controlla se chi sta cercando di rimuovere il file ha la lock
        Pthread_mutex_lock(&(file->mutex_file));
        file->isLocked = 0;
        pthread_cond_signal(&(file->wait_lock));
        Pthread_mutex_unlock(&(file->mutex_file));

        Pthread_mutex_unlock(&mutex_filehistory);
        Pthread_mutex_unlock(&mutex_storage); 
        free(path);
        res = EPERM;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    file->isDeleted = 1;
    pthread_cond_broadcast(&(file->wait_lock)); //Informa tutti i worker in attesa che il file è stato eliminato

    //Comunica a tutti i client in attesa di lock che non è possibile ottenerla
    int *fd_waiting;
    res = ENOENT;
    while ((fd_waiting = list_pop(file->pendingLock)) != NULL) {
        writen(*fd_waiting, &res, sizeof(int));
        free(fd_waiting);
    }

    //Rimuove path da fileHistory
    if (list_delete(fileHistory, path, NULL) == -1) { //Non libero la memoria perché verrà liberata dalla icl_hash_delete
        free(path);
        res = EINVAL;
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(file->mutex_file)); file->isLocked = 0; Pthread_mutex_unlock(&(file->mutex_file)); Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        return -1;
    }

    int byteDim = file->byteDim;

    //Rimuove il file dallo storage
    if (icl_hash_delete(fileStorage->files, path, NULL, freeFile) == -1) {
        free(path);
        res = EINVAL;
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(file->mutex_file)); file->isLocked = 0; Pthread_mutex_unlock(&(file->mutex_file)); Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        return -1;
    }

    //Aggiorna le informazioni dello storage
    fileStorage->actual_numFile--;
    fileStorage->actual_space -= byteDim;

    free(path);
    Pthread_mutex_unlock(&mutex_filehistory);
    Pthread_mutex_unlock(&mutex_storage);

    //Invia esito positivo al client
    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

//Assegna la lock ad un client su un file
int lock_file(int fd) {
    int len;
    int res = -1;
    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in appendFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    //Ricerca del file all'interno del file storage
    file_t *f = (file_t*) icl_hash_find(fileStorage->files, path);
    free(path);
    if (f == NULL) { //File non presente nello storage
        Pthread_mutex_unlock(&mutex_storage);
        fprintf(stderr, "File non presente nello storage\n");

        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }
    Pthread_mutex_lock(&(f->mutex_file));
    Pthread_mutex_unlock(&mutex_storage);    

    while(f->isLocked && !f->isDeleted)
        pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));

    //Controlla se il file è stato cancellato
    if (f->isDeleted) {
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    f->isLocked = 1;
    Pthread_mutex_unlock(&(f->mutex_file));

    //Controlla che il client abbia aperto quel file
    if (list_find(f->clients, &fd) == NULL) {
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));

        fprintf(stderr, "File non aperto dal client\n");
        res = EACCES;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    //Controlla se il file non è lockato oppure se il file è già lockato dal client
    if (f->lock == 0 || f->lock == fd) {
        f->lock = fd;

        //Invia esito positivo al client
        res = 0;
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)))
    } else {
        int *lock_fd = malloc(sizeof(int));
        EQ_NULL_EXIT(lock_fd, "malloc in lock_file")
        *lock_fd = fd;
        //Aggiunge il client alla lista dei client che stanno aspettando di ottenere una lock
        SYSCALL_ONE_RETURN_F(list_append(f->pendingLock, lock_fd), "list_append in lock_file", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)); res = ECANCELED; SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen"))
    }

    Pthread_mutex_lock(&(f->mutex_file));
    f->isLocked = 0;
    pthread_cond_signal(&(f->wait_lock));
    Pthread_mutex_unlock(&(f->mutex_file));

    return 0;
}

//Rimuove la lock ad un client su un file
int unlock_file(int fd) {
    int len;
    int res;
    //Lettura della lunghezza del path
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", res = EINTR; writen(fd, &res, sizeof(int)))

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in appendFile")

    //Lettura del path
    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path); res = EINTR; writen(fd, &res, sizeof(int)))

    Pthread_mutex_lock(&mutex_storage);
    //Ricerca del file all'interno dello storage
    file_t *f = (file_t*) icl_hash_find(fileStorage->files, path);
    free(path);
    if (f == NULL) { //File non presente nello storage
        Pthread_mutex_unlock(&mutex_storage);
        fprintf(stderr, "File non presente nello storage\n");

        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }
    Pthread_mutex_lock(&(f->mutex_file));
    Pthread_mutex_unlock(&mutex_storage);

    while(f->isLocked && !f->isDeleted)
        pthread_cond_wait(&(f->wait_lock), &(f->mutex_file));

    //Controlla se il file è stato cancellato
    if (f->isDeleted) {
        res = ENOENT;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
        return -1;
    }

    f->isLocked = 1;
    Pthread_mutex_unlock(&(f->mutex_file));
    
    //Controlla che il client abbia aperto quel file
    if (list_find(f->clients, &fd) == NULL) {
        Pthread_mutex_lock(&(f->mutex_file));
        f->isLocked = 0;
        Pthread_mutex_unlock(&(f->mutex_file));

        fprintf(stderr, "File non aperto dal client\n");
        res = EACCES;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
    }

    //Controlla se il client detiene la lock sul file
    if (f->lock == fd) {
        //Concede la lock al prossimo client in attesa
        int *lock_fd = list_pop(f->pendingLock);

        if (lock_fd == NULL) {
            f->lock = 0;
        } else {
            f->lock = *lock_fd;
            free(lock_fd);

            //Invia al client che era in attesa esito positivo
            res = 0;
            SYSCALL_ONE_RETURN_F(writen(f->lock, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)))
        }

        //Invia esito positivo al client
        res = 0;
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)))
    } else {
        //Il client non deteneva la lock sul file, gli viene inviato un codice d'errore
        res = EPERM;
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_lock(&(f->mutex_file)); f->isLocked = 0; Pthread_mutex_unlock(&(f->mutex_file)))
    }
    
    Pthread_mutex_lock(&(f->mutex_file));
    f->isLocked = 0;
    pthread_cond_signal(&(f->wait_lock));
    Pthread_mutex_unlock(&(f->mutex_file));

    return 0;
}

//Implementa il punto di partenza di tutti i worker. Si occupa di invocare le funzioni che soddisfano
//le richieste dei client connessi
void* workerThread(void* arg) {
    int Wendpoint = *(int*) arg; //Endpoint di scrittura verso il thread dispatcher
    
    while(!sigCaught) {
        int fd;

        Pthread_mutex_lock(&mutex_connections);

        while (connection->head == NULL && !sigCaught)
            pthread_cond_wait(&cond_emptyConnections, &mutex_connections);
        
        if (sigCaught) {
            Pthread_mutex_unlock(&mutex_connections);
            return NULL;
        }

        //Preleva una nuova richiesta
        int *tmp;
        EQ_NULL_EXIT_F(tmp = (int*) list_pop(connection), "list_pop", Pthread_mutex_unlock(&mutex_connections))
        fd = *tmp;
        free(tmp);

        Pthread_mutex_unlock(&mutex_connections);

        int opt = -1; //Se dovesse interrompersi la connessione il fd non viene messo nel set
        if(readn(fd, &opt, sizeof(int)) == -1) {
            if (writen(Wendpoint, &fd, sizeof(int)) == -1) { //In caso di errore comunica al thread dispatcher di non accettare più richieste da quel client
                perror("writen in thread worker");
                close(fd);
            }
            continue;
        }

        int closeFd = 0;

        //In caso di errore viene chiuso il fd
        switch (opt) {
            case FIND_FILE: if (findFile(fd) != 0) closeFd = 1; break;
            case OPEN_FILE: if (openFile(fd) != 0) closeFd = 1; break;
            case END_CONNECTION: closeConnection(fd); closeFd = 1; break;
            case READ_FILE: if (readFile(fd) != 0) closeFd = 1; break;
            case READN_FILE: if (readnFile(fd) != 0) closeFd = 1; break;
            case WRITE_FILE: if (writeFile(fd) != 0) closeFd = 1; break;
            case APPEND_FILE: if (appendFile(fd) != 0) closeFd = 1; break;
            case CLOSE_FILE: if (closeFile(fd) != 0) closeFd = 1; break;
            case REMOVE_FILE: if (removeFile(fd) != 0) closeFd = 1; break;
            case LOCK_FILE: if (lock_file(fd) != 0) closeFd = 1; break;
            case UNLOCK_FILE: if (unlock_file(fd) != 0) closeFd = 1; break;
            default: {closeFd = 1; break;}
        }

        if (opt == -1 || closeFd) {
            close(fd);
            fd *= -1;
        }

        //Invia il fd al thread dispatcher
        if (writen(Wendpoint, &fd, sizeof(int)) == -1) {
            perror("writen in thread worker");
            exit(EXIT_FAILURE);
        }
    }

    return NULL;
}

void initializeSocket(int* fd_socket) {
    *fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    SYSCALL_ONE_EXIT(*fd_socket, "socket")

    struct sockaddr_un sa;
    strncpy(sa.sun_path, socketName, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    
    SYSCALL_ONE_EXIT(bind(*fd_socket, (struct sockaddr *) &sa, sizeof(sa)), "bind")
    SYSCALL_ONE_EXIT(listen(*fd_socket, MAXCONN), "listen")
}

pthread_t* spawnThread(int W, int *writeEndpoint) {
    pthread_t *tidLst = malloc(sizeof(pthread_t) * W);
    if (tidLst == NULL) return NULL;
    for(int i = 0; i < W; i++) {
        int err;
        SYSCALL_NOT_ZERO_EXIT(err, pthread_create(&(tidLst[i]), NULL, workerThread, (void*) writeEndpoint), "pthread_create")
    }

    return tidLst;
}

//Trova il nuovo massimo per il set
int updateSet(fd_set *set, int fdMax) {
    for(int i = fdMax; i >= 0; i--) {
        if (FD_ISSET(i, set)) return i;
    }
    return 0;
}

//Chiude tutte le connessioni attive nel set
void closeConnections(fd_set *set, int max) {
    for(int i = 0; i < max + 1; i++) {
        if (FD_ISSET(i, set)) {
            close(i);
            FD_CLR(i, set);
        }
    }
}

//Controlla se c'è ancora almeno una connessione attiva
int checkConnections(fd_set *set, int max) {
    for(int i = 0; i < max + 1; i++) {
        if (FD_ISSET(i, set)) return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments...\n");
        return EXIT_FAILURE;
    }

    setHandlers();

    //Inizializzazione storage
    EQ_NULL_EXIT(fileStorage = initializeStorage(fileStorage), "fileStorage")
    EQ_NULL_EXIT_F(fileHistory = list_create(fileHistory, str_compare), "list_create", icl_hash_destroy(fileStorage->files, NULL, freeFile))
    EQ_NULL_EXIT_F(connection = list_create(connection, int_compare), "list_create", icl_hash_destroy(fileStorage->files, NULL, freeFile); list_destroy(fileHistory, free))
    
    //Creazione pipe per ricevere la notifica della gestione di un segnale
    int signalPipe[2];
    SYSCALL_ONE_EXIT(pipe(signalPipe), "pipe");

    //Creazione del thread che gestisce i segnali
    int err;
    pthread_t sighandler_thread;
    SYSCALL_NOT_ZERO_EXIT(err, pthread_create(&sighandler_thread, NULL, sighandler, (void*) &signalPipe[1]), "pthread_create")

    //Parsing del file di config
    int numW;
    if (parseFile(argv[1], &numW, &(fileStorage->max_space), &(fileStorage->max_file), &socketName, &logFile) != 0) {
        fprintf(stderr, "Error parsing %s\n", argv[1]);
        freeGlobal();
        return EXIT_FAILURE;
    }

    //Creazione pipe per comunicare con i thread worker
    int fdPipe[2];
    SYSCALL_ONE_EXIT(pipe(fdPipe), "pipe");

    //Creazione dei thread worker
    pthread_t *tidLst;
    if ((tidLst = spawnThread(numW, &fdPipe[1])) == NULL) exit(EXIT_FAILURE);

    int listenSocket;
    initializeSocket(&listenSocket);

    //Set per gestire le connessioni
    int fdMax = 0;
    if (listenSocket > fdMax) fdMax = listenSocket;
    if (fdPipe[0] > fdMax) fdMax = fdPipe[0];
    if (signalPipe[0] > fdMax) fdMax = signalPipe[0];
    fd_set set, rdset;
    FD_ZERO(&set);

    //Setta gli endpoint di cui tener conto
    FD_SET(listenSocket, &set);
    FD_SET(fdPipe[0], &set);
    FD_SET(signalPipe[0], &set);

    Pthread_mutex_lock(&mutex_fdset);
    FD_ZERO(&setConnected);
    Pthread_mutex_unlock(&mutex_fdset);

    int stillConnected = 1;
    
    while(stillConnected) {
        rdset = set;

        if (select(fdMax + 1, &rdset, NULL, NULL, NULL) == -1) {
            if (errno == EINTR && sigCaught) break;
            perror("select");
            freeGlobal();
            return -1;
        }

        for(int fd = 0; fd < fdMax + 1; fd++) {
            if (FD_ISSET(fd, &rdset)) {
                if (stopRequests) { //Chiude tutte le connessioni attive
                    Pthread_mutex_lock(&mutex_fdset);
                    closeConnections(&setConnected, connectedMax);
                    Pthread_mutex_unlock(&mutex_fdset);
                    break;
                }

                if (fd == fdPipe[0]) { //Messaggio dal worker
                    int c_fd;

                    if (readn(fd, &c_fd, sizeof(int)) == -1) {
                        perror("read");
                        return errno;
                    }

                    if (c_fd < 0) { //Rimuove il client
                        c_fd *= -1;

                        Pthread_mutex_lock(&mutex_fdset);
                        FD_CLR(c_fd, &setConnected);
                        connectedMax = updateSet(&setConnected, connectedMax);
                        Pthread_mutex_unlock(&mutex_fdset);
                        
                        break;
                    }

                    //Il thread dispatcher si rimette in ascolto di una richiesta da c_fd
                    FD_SET(c_fd, &set);
                    if (c_fd > fdMax) fdMax = c_fd;
                    break;
                }

                //Nuova connessione
                if (fd == listenSocket && !stopConnections) {
                    int newFd = accept(listenSocket, NULL, 0);

                    if (newFd == -1) {
                        fprintf(stderr, "An error has occurred accepting connection\n");
                        break;
                    }

                    //Il thread dispatcher si mette in ascolto di una richiesta da newFd
                    FD_SET(newFd, &set);
                    if (newFd > fdMax) fdMax = newFd;

                    //Aggiunge newFd al set delle connessioni attive
                    Pthread_mutex_lock(&mutex_fdset);
                    FD_SET(newFd, &setConnected);
                    if (newFd > connectedMax) connectedMax = newFd;
                    Pthread_mutex_unlock(&mutex_fdset);

                    printf("Client connesso\n");
                    fflush(stdout);
                    break;
                }

                //Nuova richiesta
                if (fd != fdPipe[0] && fd != listenSocket && fd != signalPipe[0]) {
                    //Rimuove il fd dalle connessioni che il thread dispatcher deve ascoltare
                    FD_CLR(fd, &set);
                    fdMax = updateSet(&set, fdMax);

                    int *new = malloc(sizeof(int));
                    EQ_NULL_EXIT(new, "malloc")

                    *new = fd;

                    Pthread_mutex_lock(&mutex_connections);

                    //Aggiunge in coda la nuova richiesta
                    SYSCALL_ONE_EXIT(list_append(connection, new), "list_append")
                    
                    pthread_cond_signal(&cond_emptyConnections);
                    Pthread_mutex_unlock(&mutex_connections);

                    break;
                }

                //Segnale catturato
                if (fd == signalPipe[0]) {
                    FD_CLR(fd, &set);
                    break;
                }
            }
        }
        
        //Catturato SIGHUP
        if (stopConnections) {
            Pthread_mutex_lock(&mutex_fdset);
            stillConnected = checkConnections(&setConnected, connectedMax);
            Pthread_mutex_unlock(&mutex_fdset);
        }

    }

    //Attesa della terminazione del signal handler thread
    SYSCALL_NOT_ZERO_EXIT(err, pthread_join(sighandler_thread, NULL), "pthread_join")

    printf("\nNumero di file massimo memorizzato nel server: %d\nDimensione massima in MBytes raggiunta dal file storage: %f\nNumero di volte in cui l'algoritmo di rimpiazzamento della cache è stato eseguito per selezionare uno o più file vittima: %d\n", fileStorage->max_file_reached, (double)fileStorage->max_space_reached/(1024*1024), fileStorage->replacementPolicy);
    printf("Lista dei file contenuti nello storage al momento della chiusura del server:\n");
    
    //Notifica a tutti i worker della terminazione
    sigCaught = 1;
    pthread_cond_broadcast(&cond_emptyConnections);

    //Attesa di terminazione di tutti i worker
    for(int i = 0; i < numW; i++) {
        pthread_join(tidLst[i], NULL);
    }
    free(tidLst);

    //Libera la memoria
    char* file;
    while((file = list_pop(fileHistory)) != NULL) printf("%s\n", file); //Non esegue la free perché l'elemento nella lista è lo stesso che è presente nella chiave della hashtable quindi va liberato una volta sola
    free(fileHistory);

    SYSCALL_ONE_EXIT(list_destroy(connection, free), "list_destroy")
    SYSCALL_ONE_EXIT(icl_hash_destroy(fileStorage->files, NULL, freeFile), "icl_hash_destrory")

    free(fileStorage);
    close(listenSocket);
    close(fdPipe[0]);
    close(fdPipe[1]);
    close(signalPipe[0]);
    close(signalPipe[1]);
    SYSCALL_ONE_EXIT(unlink(socketName), "unlink");
    freeGlobal();

    return 0;
}