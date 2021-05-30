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

#include "../includes/icl_hash.h"

#define STRLEN 256
#define UNIX_PATH_MAX 108
#define MAXCONN 100
#define BUCKETS 250

icl_hash_t *storage;
long max_space = 0;
int max_file = 0;
long actual_space = 0; //Mettere tutto dentro un'unica struct che contiene anche l'hash table
int actual_numFile = 0;
int max_file_reached = 0;
long max_space_reached = 0;
int replacementPolicy = 0;
static pthread_mutex_t mutex_storage = PTHREAD_MUTEX_INITIALIZER;

list_t fileHistory;
static pthread_mutex_t mutex_filehistory = PTHREAD_MUTEX_INITIALIZER;

typedef struct _file_t {
    char* path;
    size_t byteDim;
    void* data;
    list_t clients;
} file_t;

list_t connection;
static pthread_mutex_t mutex_connections = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_emptyConnections = PTHREAD_COND_INITIALIZER;

int sigCaught = 0;
int stopRequests = 0;
int stopConnections = 0;

char* socketName;
char* logFile;

//Scrivere nella relazione che fileHistory e i file in storage hanno lo stesso path allocato quindi basta liberare uno dei due per liberare entrambi

void freeFile(void* f) {
    file_t *file = (file_t*) f;
    
    free(file->path);
    free(file->data);
    SYSCALL_ONE_EXIT(list_destroy(&(file->clients), free), "list_destroy")
    free(file);
}

int FIFO_ReplacementPolicy(long space, int numFiles, int fd) {
    printf("Esecuzione dell'algoritmo di sostituzione\n");
    fflush(stdout);

    Pthread_mutex_lock(&mutex_storage);
    replacementPolicy++;
    while(space > max_space || numFiles > max_file) {
        Pthread_mutex_unlock(&mutex_storage);

        Pthread_mutex_lock(&mutex_filehistory);
        char* oldFile_name = (char*) list_pop(&fileHistory);
        Pthread_mutex_unlock(&mutex_filehistory);

        if (oldFile_name == NULL) {
            Pthread_mutex_lock(&mutex_storage);
            continue;
        }

        Pthread_mutex_lock(&mutex_storage);
        file_t* oldFile = (file_t*) icl_hash_find(storage, oldFile_name);
        if (oldFile == NULL) { //Se c'è un'inconsistenza il file server non è più affidabile
            fprintf(stderr, "Inconsistenza tra lo storage e la cronologia dei file\n");
            exit(EXIT_FAILURE);
        }

        if (fd != -1) {
            int opt = SEND_FILE;
            if (writen(fd, &opt, sizeof(int)) == -1) {
                Pthread_mutex_unlock(&mutex_storage);
                return -1;
            }
            int len = strnlen(oldFile->path, STRLEN);
            if (writen(fd, &len, sizeof(int)) == -1) {
                Pthread_mutex_unlock(&mutex_storage);
                return -1;
            }
            if (writen(fd, oldFile->path, len * sizeof(char)) == -1) {
                Pthread_mutex_unlock(&mutex_storage);
                return -1;
            }
            if (writen(fd, &(oldFile->byteDim), sizeof(size_t)) == -1) {
                Pthread_mutex_unlock(&mutex_storage);
                return -1;
            }
            if (writen(fd, oldFile->data, oldFile->byteDim * sizeof(char)) == -1) {
                Pthread_mutex_unlock(&mutex_storage);
                return -1;
            }
        }

        space -= oldFile->byteDim;
        numFiles--;

        if (icl_hash_delete(storage, oldFile_name, NULL, freeFile) == -1) exit(EXIT_FAILURE);

        //Non eseguo la unlock perché la guardia del while valuterà le variabili condivise dello storage
    }
    Pthread_mutex_unlock(&mutex_storage);

    return 0;
}

void freeGlobal(){
    if (socketName != NULL) free(socketName);
    if (logFile != NULL) free(logFile);
}

void* sighandler(void* arg){ //Da scrivere nella relazione: Si suppone che se fallisce la sigwait tutto il processo viene terminato perché potrei non riuscire più a terminare il server.
    int sigPipe = *(int*) arg;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGHUP);

    while(!sigCaught) {
        int sig;
        int err;
        SYSCALL_NOT_ZERO_EXIT(err, sigwait(&mask, &sig), "sigwait")

        switch (sig) {
        case SIGINT:
        case SIGQUIT: stopRequests = 1;
        case SIGHUP: stopConnections = 1; break;
        }

        sigCaught = 1;

        close(sigPipe);
    }
    return NULL;
}

void setHandlers() {
    //Maschero i segnali SIGINT, SIGQUIT, SIGHUP che verranno gestiti dal thread handler
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

int parseFile(const char* filepath, int* numWorkers, long* memorySpace, int* numFile, char** sockName, char** logFile) { //Migliorare il parsing controllando che la stringa sia effettivamente solo quella
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

        if(strncmp(str, "MEM_SPACE", 9) == 0){
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

int createFile(int fd) {
    int res = -1;
    file_t *newFile = malloc(sizeof(file_t));
    EQ_NULL_EXIT_F(newFile, "malloc in createFile", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn"))

    newFile->byteDim = 0;
    newFile->data = NULL;
    int len;
    
    SYSCALL_ONE_RETURN_F(readn(fd, &len, sizeof(int)), "readn", free(newFile)) //Se fallisce la readn tornerà al chiamante, che chiuderà il fd e quindi il client se ne accorgerà

    newFile->path = calloc(len, sizeof(char));
    EQ_NULL_EXIT_F(newFile->path, "calloc in createFile", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn"))

    SYSCALL_ONE_RETURN_F(readn(fd, newFile->path, len * sizeof(char)), "readn", free(newFile->path); free(newFile))

    SYSCALL_ONE_RETURN(list_create(&(newFile->clients), int_compare), "list_create")

    Pthread_mutex_lock(&mutex_storage);
    if (actual_space + newFile->byteDim > max_space || actual_numFile + 1 > max_file) {
        Pthread_mutex_unlock(&mutex_storage);
        if (FIFO_ReplacementPolicy(actual_space + newFile->byteDim, actual_numFile + 1, -1) == -1) {
            fprintf(stderr, "Errore nell'algoritmo di esecuzione\n");
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")
            return -1;
        }
        Pthread_mutex_lock(&mutex_storage);
    }

    if (icl_hash_insert(storage, newFile->path, newFile) == NULL) {
        fprintf(stderr, "Errore in icl_hash_insert\n");
        SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
        exit(EXIT_FAILURE);
    }
    actual_space += newFile->byteDim;
    actual_numFile++;
    if (actual_space > max_space_reached) max_space_reached = actual_space;
    if (actual_numFile > max_file_reached) max_file_reached = actual_numFile;
    Pthread_mutex_unlock(&mutex_storage);

    Pthread_mutex_lock(&mutex_filehistory);
    if (list_append(&fileHistory, newFile->path) == -1) {
        perror("list_append");
        SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
        exit(EXIT_FAILURE);
    }
    Pthread_mutex_unlock(&mutex_filehistory);

    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")

    return 0;
}

int findFile(int fd) {
    int len;
    int found;
    SYSCALL_ONE_RETURN(readn(fd, &len, sizeof(int)), "readn")

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT(path, "calloc in findFile")

    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path))

    Pthread_mutex_lock(&mutex_storage);
    if(icl_hash_find(storage, path) == NULL)
        found = 0;
    else
        found = 1;
    Pthread_mutex_unlock(&mutex_storage);
    
    free(path);

    SYSCALL_ONE_RETURN(writen(fd, &found, sizeof(int)), "writen")

    return 0;
}

int openFile(int fd) {
    int len;
    int res = -1;
    SYSCALL_ONE_RETURN(readn(fd, &len, sizeof(int)), "readn")

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT_F(path, "calloc in openFile", SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn"))

    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path))

    Pthread_mutex_lock(&mutex_storage);
    file_t *f = (file_t*) icl_hash_find(storage, path);
    free(path);
    if (f == NULL) {
        fprintf(stderr, "File non presente nello storage\n");
        Pthread_mutex_unlock(&mutex_storage);
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")
        return -1;
    }

    //Se il file è già aperto restituisce esito positivo al client. Non effettua cambiamenti
    if (list_find(&(f->clients), &fd) == NULL) {
        Pthread_mutex_unlock(&mutex_storage);
        res = 0;
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")
        return 0;
    }

    int *newClient = malloc(sizeof(int));
    EQ_NULL_EXIT_F(newClient, "malloc error in openFile", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn"))

    *newClient = fd;

    if(list_push(&(f->clients), newClient) == -1) {
        perror("list_push");
        Pthread_mutex_unlock(&mutex_storage);
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "readn", free(newClient))
        return -1;
    }
    Pthread_mutex_unlock(&mutex_storage);

    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")
    
    return 0;
}

int closeConnection(int fd, int endpoint) {
    int numFiles;
    int res = -1;
    SYSCALL_ONE_RETURN(readn(fd, &numFiles, sizeof(int)), "readn")

    for(int i = 0; i < numFiles; i++) {
        int len;
        SYSCALL_ONE_RETURN(readn(fd, &len, sizeof(int)), "readn")
        
        char* path = calloc(len, sizeof(char));
        EQ_NULL_EXIT_F(path, "calloc in closeConnection", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn"))

        SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path))

        Pthread_mutex_lock(&mutex_storage);
        file_t *f = (file_t*) icl_hash_find(storage, path);
        if (f == NULL) {
            fprintf(stderr, "File non presente nello storage\n");
            Pthread_mutex_unlock(&mutex_storage);
            SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "readn", free(path))
        }

        free(path);

        SYSCALL_ONE_RETURN_F(list_delete(&(f->clients), &fd, free), "list_delete in closeConnection", SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn"))
        Pthread_mutex_unlock(&mutex_storage);
    }

    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")

    int msg = -1;
    SYSCALL_ONE_RETURN(writen(endpoint, &msg, sizeof(int)), "readn")
    // close(fd);
}

int readFile(int fd) {
    int len;
    int res = -1;
    SYSCALL_ONE_RETURN(readn(fd, &len, sizeof(int)), "readn")

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT_F(path, "calloc in readFile", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn"))

    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path);)

    Pthread_mutex_lock(&mutex_storage);
    file_t *f = (file_t*) icl_hash_find(storage, path);
        if (f == NULL) {
            Pthread_mutex_unlock(&mutex_storage);
            fprintf(stderr, "File non presente nello storage\n");
            SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "readn", free(path))
        }
    
    free(path);

    //Controlla che il client abbia aperto quel file
    if (list_find(&(f->clients), &fd) == NULL) {
        Pthread_mutex_unlock(&mutex_storage);
        fprintf(stderr, "File non aperto dal client\n");
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")
    }

    //Comunica al Client che non ci sono stati errori a recuperare il file
    res = 0;
    SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "readn", Pthread_mutex_unlock(&mutex_storage))

    //Invia al client la dimensione e il contenuto del file
    SYSCALL_ONE_RETURN_F(writen(fd, &(f->byteDim), sizeof(size_t)), "writen", Pthread_mutex_unlock(&mutex_storage))
    SYSCALL_ONE_RETURN_F(writen(fd, f->data, f->byteDim * sizeof(char)), "writen", Pthread_mutex_unlock(&mutex_storage))
    Pthread_mutex_unlock(&mutex_storage);

    return 0;
}

int readnFile(int fd) {
    int N;
    SYSCALL_ONE_RETURN(readn(fd, &N, sizeof(int)), "readn")

    Pthread_mutex_lock(&mutex_storage);
    if (N <= 0 || N > actual_numFile) N = actual_numFile;
    
    Pthread_mutex_lock(&mutex_filehistory);
    node_t* state = NULL;
    char* path = (char*) list_getNext(&fileHistory, &state);
    int i = 0;
    while(i < N && path != NULL) {
        file_t *f = icl_hash_find(storage, path);
        if (f == NULL) {
            fprintf(stderr, "Errore critico: Inconsistenza tra storage e fileHistory\n");
            exit(EXIT_FAILURE);
        }

        int opt = SEND_FILE;
        SYSCALL_ONE_RETURN_F(writen(fd, &opt, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        int len = strnlen(path, STRLEN) + 1;
        SYSCALL_ONE_RETURN_F(writen(fd, &len, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        SYSCALL_ONE_RETURN_F(writen(fd, path, len * sizeof(char)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        SYSCALL_ONE_RETURN_F(writen(fd, &(f->byteDim), sizeof(size_t)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        SYSCALL_ONE_RETURN_F(writen(fd, f->data, f->byteDim * sizeof(char)), "writen", Pthread_mutex_unlock(&mutex_filehistory); Pthread_mutex_unlock(&mutex_storage))
        i++;

        path = (char*) list_getNext(NULL, &state);
    } 
    Pthread_mutex_unlock(&mutex_filehistory);
    Pthread_mutex_unlock(&mutex_storage);

    int res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

int appendFile(int fd) {
    int len;
    int res = -1;
    SYSCALL_ONE_RETURN(readn(fd, &len, sizeof(int)), "readn")

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT_F(path, "calloc in appendFile", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn"))

    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path))

    Pthread_mutex_lock(&mutex_storage);
    file_t *f = (file_t*) icl_hash_find(storage, path);
        if (f == NULL) {
            Pthread_mutex_unlock(&mutex_storage);
            fprintf(stderr, "File non presente nello storage\n");
            SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "readn", free(path))
        }
    
    free(path);

    //Controlla che il client abbia aperto quel file
    if (list_find(&(f->clients), &fd) == NULL) {
        Pthread_mutex_unlock(&mutex_storage);
        fprintf(stderr, "File non aperto dal client\n");
        SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "readn")
    }
    
    size_t fileDim;
    SYSCALL_ONE_RETURN_F(readn(fd, &fileDim, sizeof(size_t)), "readn", Pthread_mutex_unlock(&mutex_storage))

    char* newData = calloc(fileDim, sizeof(char));
    EQ_NULL_EXIT_F(newData, "malloc", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn"))

    SYSCALL_ONE_RETURN_F(readn(fd, newData, fileDim * sizeof(char)), "readn", free(newData); Pthread_mutex_unlock(&mutex_storage))

    if (actual_space + fileDim > max_space) {
        Pthread_mutex_unlock(&mutex_storage);
        if (FIFO_ReplacementPolicy(actual_space + fileDim, actual_numFile, fd) == -1) {
            fprintf(stderr, "Errore nell'esecuzione dell'algoritmo di sostituzione\n");
            SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")
            return -1;
        }
        Pthread_mutex_lock(&mutex_storage);
    }

    if (f->data == NULL)
        f->data = newData;
    else {
        char* tmp = realloc(f->data, (f->byteDim + fileDim) * sizeof(char));
        EQ_NULL_EXIT_F(tmp, "realloc in appendFile", SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "writen"))
        
        f->data = tmp;
        memcpy((char*)f->data + f->byteDim, newData, fileDim);
    }
    f->byteDim += fileDim;
    actual_space += fileDim;
    Pthread_mutex_unlock(&mutex_storage);

    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

int closeFile(int fd) {
    int len;
    int res = -1;
    SYSCALL_ONE_RETURN(readn(fd, &len, sizeof(int)), "readn")

    char* path = calloc(len, sizeof(char));
    EQ_NULL_EXIT_F(path, "calloc in closeFile", SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen"))

    SYSCALL_ONE_RETURN_F(readn(fd, path, len * sizeof(char)), "readn", free(path))

    Pthread_mutex_lock(&mutex_storage);
    file_t *file = (file_t*) icl_hash_find(storage, path);
    free(path);

    if (file == NULL) {
        fprintf(stderr, "File non presente nello storage\n");
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_storage))
        return -1;
    }

    if (list_delete(&(file->clients), &fd, free) == -1) {
        SYSCALL_ONE_RETURN_F(writen(fd, &res, sizeof(int)), "writen", Pthread_mutex_unlock(&mutex_storage))
        return -1;
    }
    Pthread_mutex_unlock(&mutex_storage);

    res = 0;
    SYSCALL_ONE_RETURN(writen(fd, &res, sizeof(int)), "writen")

    return 0;
}

void* workerThread(void* arg) {
    int Wendpoint = *(int*) arg;
    while(!sigCaught) {
        int fd;
        Pthread_mutex_lock(&mutex_connections);

        while (connection.head == NULL && !sigCaught)
            pthread_cond_wait(&cond_emptyConnections, &mutex_connections);
        
        if (sigCaught) {
            Pthread_mutex_unlock(&mutex_connections);
            return NULL;
        }

        int *tmp;
        EQ_NULL_EXIT_F(tmp = (int*) list_pop(&connection), "list_pop", Pthread_mutex_unlock(&mutex_connections))
        fd = *tmp;
        free(tmp);

        Pthread_mutex_unlock(&mutex_connections);

        int opt = -1; //Se dovesse interrompersi la connessione il fd non viene messo nel set
        if(readn(fd, &opt, sizeof(int)) == -1) {
            if (writen(Wendpoint, &fd, sizeof(int)) == -1) {
                perror("writen in thread worker");
                close(fd);
            }
            continue;
        }

        int closeFd = 0;

        switch (opt) {
            case FIND_FILE: if (findFile(fd) != 0) {closeFd = 1; break;}
            case CREATE_FILE: if (createFile(fd) != 0) { closeFd = 1; break;}
            case OPEN_FILE: if (openFile(fd) != 0) {closeFd = 1; break;}
            case END_CONNECTION: closeConnection(fd, Wendpoint); closeFd = 1; break;
            case READ_FILE: if (readFile(fd) != 0) {closeFd = 1; break;}
            case READN_FILE: if (readnFile(fd) != 0) {closeFd = 1; break;}
            case APPEND_FILE: if (appendFile(fd) != 0) {closeFd = 1; break;}
            case CLOSE_FILE: if (closeFile(fd) != 0) {closeFd = 1; break;}
            default: {closeFd = 1; break;}
        }

        if (opt == -1 || closeFd) {
            close(fd);
            fd *= -1;
        }

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

void spawnThread(int W, int *writeEndpoint) {
    for(int i = 0; i < W; i++) {
        int err;
        pthread_t tid;
        SYSCALL_NOT_ZERO_EXIT(err, pthread_create(&tid, NULL, workerThread, (void*) writeEndpoint), "pthread_create")

        SYSCALL_NOT_ZERO_EXIT(err, pthread_detach(tid), "pthread_detach")
    }
}

int updateSet(fd_set *set, int fdMax) {
    for(int i = fdMax; i >= 0; i--) {
        if (FD_ISSET(i, set)) return i;
    }
    return 0;
}

void closeConnections(fd_set *set, int max, int fd1, int fd2, int fd3) {
    for(int i = 0; i < max + 1; i++) {
        if (FD_ISSET(i, set) && i != fd1 && i != fd2 && i != fd3) close(i);
    }
}

int checkConnections(fd_set *set, int max, int fd1, int fd2, int fd3) {
    for(int i = 0; i < max; i++) {
        if (FD_ISSET(i, set) && i != fd1 && i != fd2 && i != fd3) return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments...\n");
        return EXIT_FAILURE;
    }

    setHandlers();

    //Inizializzazione tabella hash
    EQ_NULL_EXIT(storage = icl_hash_create(BUCKETS, hash_pjw, string_compare), "icl_hash_create")
    if (list_create(&fileHistory, str_compare) == -1) {
        perror("list_create");
        exit(EXIT_FAILURE);
    }
    if (list_create(&connection, int_compare) == -1) {
        perror("list_create");
        list_destroy(&fileHistory, free);
        exit(EXIT_FAILURE);
    }
    
    int signalPipe[2];
    SYSCALL_ONE_EXIT(pipe(signalPipe), "pipe");

    int err;
    pthread_t sighandler_thread;
    SYSCALL_NOT_ZERO_EXIT(err, pthread_create(&sighandler_thread, NULL, sighandler, (void*) &signalPipe[1]), "pthread_create")

    int numW;
    if (parseFile(argv[1], &numW, &max_space, &max_file, &socketName, &logFile) != 0) {
        fprintf(stderr, "Error parsing %s\n", argv[1]);
        freeGlobal();
        return EXIT_FAILURE;
    }

    int fdPipe[2];
    SYSCALL_ONE_EXIT(pipe(fdPipe), "pipe");

    spawnThread(numW, &fdPipe[1]); //Aggiungere freeGlobal

    int listenSocket;
    initializeSocket(&listenSocket); //Aggiungere freeGlobal

    int fdMax = 0;
    if (listenSocket > fdMax) fdMax = listenSocket;
    if (fdPipe[0] > fdMax) fdMax = fdPipe[0];
    if (signalPipe[0] > fdMax) fdMax = signalPipe[0];
    fd_set set, rdset;
    FD_ZERO(&set);
    FD_SET(listenSocket, &set);
    FD_SET(fdPipe[0], &set);
    FD_SET(signalPipe[0], &set);

    int stillConnected = 1;
    
    while(stillConnected) { //Bisogna aggiungere che se non ci sono più client connessi e stopConnections è attivo, termina tutto.
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
                    closeConnections(&set, fdMax, listenSocket, fdPipe[0], signalPipe[0]);
                    break;
                }

                // if (fd == signalPipe[0]) break; //Questo non ci va perché sennò non gestisce bene il tipo di segnale che gli arriva

                if (fd == fdPipe[0]) { //Invece che mandare -1 potrei mandare -fd che vorrebbe dire "Elimina questo fd dal set"
                    int c_fd;

                    if (readn(fd, &c_fd, sizeof(int)) == -1) {
                        perror("read");
                        return errno;
                    }

                    if (c_fd == -1) continue;

                    FD_SET(c_fd, &set);
                    if (c_fd > fdMax) fdMax = c_fd;
                    continue;
                }

                if (fd == listenSocket && !stopConnections) {
                    int newFd = accept(listenSocket, NULL, 0);

                    if (newFd == -1) {
                        fprintf(stderr, "An error has occurred accepting connection\n");
                        continue;
                    }

                    FD_SET(newFd, &set);
                    if (newFd > fdMax) fdMax = newFd;

                    continue;
                }

                if (fd != fdPipe[0] && fd != listenSocket) { //Ragionare se ho modificato correttamente
                    FD_CLR(fd, &set);
                    fdMax = updateSet(&set, fdMax);

                    int *new = malloc(sizeof(int));
                    EQ_NULL_EXIT(new, "malloc") //Aggiungere freeGlobal. Se si verifica non viene cancellato il socket

                    *new = fd;

                    Pthread_mutex_lock(&mutex_connections);

                    SYSCALL_ONE_EXIT(list_append(&connection, new), "list_append")
                    
                    pthread_cond_signal(&cond_emptyConnections);
                    Pthread_mutex_unlock(&mutex_connections);
                }
            }
        }

        if (stopConnections) stillConnected = checkConnections(&set, fdMax, listenSocket, fdPipe[0], signalPipe[0]); //Ricontrollare

    }


    SYSCALL_NOT_ZERO_EXIT(err, pthread_join(sighandler_thread, NULL), "pthread_join")

    printf("Numero di file massimo memorizzato nel server: %d\nDimensione massima in MBytes raggiunta dal file storage: %f\nNumero di volte in cui l'algoritmo di rimpiazzamento della cache è stato eseguito per selezionare uno o più file vittima: %d\n", max_file_reached, (double)max_space_reached/(1024*1024), replacementPolicy);
    printf("Lista dei file contenuti nello storage al momento della chiusura del server:\n");
    Pthread_mutex_lock(&mutex_connections); //Forse non è necessario
    char* file;
    while((file = list_pop(&fileHistory)) != NULL) printf("%s\n", file); //Non esegue la free perché l'elemento nella lista è lo stesso che è presente nella chiave della hashtable quindi va liberato una volta sola
    Pthread_mutex_unlock(&mutex_connections);

    SYSCALL_ONE_EXIT(list_destroy(&connection, free), "list_destroy")
    SYSCALL_ONE_EXIT(icl_hash_destroy(storage, NULL, freeFile), "icl_hash_destrory")
    close(listenSocket);
    close(fdPipe[0]);
    close(fdPipe[1]);
    close(signalPipe[0]);
    close(signalPipe[1]);
    SYSCALL_ONE_EXIT(unlink(socketName), "unlink");
    freeGlobal();

    return 0;
}