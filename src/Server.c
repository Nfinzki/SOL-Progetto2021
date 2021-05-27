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
long max_space;
int max_file;
long actual_space = 0; //Mettere tutto dentro un'unica struct che contiene anche l'hash table
static pthread_mutex_t mutex_storage = PTHREAD_MUTEX_INITIALIZER;

list_t fileHistory;
static pthread_mutex_t mutex_filehistory = PTHREAD_MUTEX_INITIALIZER;

typedef struct _file_t {
    char* path;
    int byteDim;
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


void freeFile(void* f) {
    file_t *file = (file_t*) f;
    
    free(file->path);
    SYSCALL_ONE_EXIT(list_destroy(&(file->clients), free), "list_destroy")
    free(file);
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

        sigCaught = 1; //Probabilmente va tolto

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

void createFile(int fd) { //Renderli int?
    file_t *newFile = malloc(sizeof(file_t));

    if (newFile == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    SYSCALL_ONE_EXIT(readn(fd, &(newFile->byteDim), sizeof(int)), "readn")

    newFile->path = calloc(newFile->byteDim, sizeof(char));
    if (newFile->path == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    SYSCALL_ONE_EXIT(readn(fd, newFile->path, newFile->byteDim * sizeof(char)), "readn")
    
    if(list_create(&(newFile->clients), int_compare) == -1) {
        perror("list_create");
        exit(EXIT_FAILURE);
    }

    if (actual_space + newFile->byteDim > max_space) {
        printf("Entra in azione l'argoritmo di sostituzione\n");
        fflush(stdout);
    } else {
        Pthread_mutex_lock(&mutex_storage);
        if (icl_hash_insert(storage, newFile->path, newFile) == NULL) {
            fprintf(stderr, "Error in icl_hash_insert\n");
            int res = -1;
            SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
            exit(EXIT_FAILURE);
        }
        actual_space += newFile->byteDim;
        Pthread_mutex_unlock(&mutex_storage);

        Pthread_mutex_lock(&mutex_filehistory);
        if (list_append(&fileHistory, newFile->path) == -1) {
            perror("list_append");
            int res = -1;
            SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
            exit(EXIT_FAILURE);
        }
        Pthread_mutex_unlock(&mutex_filehistory);
    }

    int res = 0;
    SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")

}

void findFile(int fd) {
    int len;
    SYSCALL_ONE_EXIT(readn(fd, &len, sizeof(int)), "readn")

    char* path = calloc(len, sizeof(char));
    if (path == NULL) {
        perror("calloc");
        //List distroy. Un po' ovunque
        exit(EXIT_FAILURE);
    }
    SYSCALL_ONE_EXIT(readn(fd, path, len * sizeof(char)), "readn")

    Pthread_mutex_lock(&mutex_storage);
    int found;
    if(icl_hash_find(storage, path) == NULL)
        found = 0;
    else
        found = 1;
    Pthread_mutex_unlock(&mutex_storage);
    
    free(path);

    SYSCALL_ONE_EXIT(writen(fd, &found, sizeof(int)), "writen")
}

void openFile(int fd) { //Se il file è già aperto cosa succede?
    int len, res;
    SYSCALL_ONE_EXIT(readn(fd, &len, sizeof(int)), "readn")

    char* path = calloc(len, sizeof(char));
    if (path == NULL) {
        perror("calloc in openFile");
        //List distroy. Un po' ovunque
        exit(EXIT_FAILURE);
    }
    SYSCALL_ONE_EXIT(readn(fd, path, len * sizeof(char)), "readn")

    Pthread_mutex_lock(&mutex_storage);
    file_t *f = (file_t*) icl_hash_find(storage, path);
    if (f == NULL) {
        fprintf(stderr, "File non presente nello storage\n");
        res = 0;
        SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
        exit(EXIT_FAILURE);
    }

    int *newClient = malloc(sizeof(int));
    if (newClient == NULL) {
        fprintf(stderr, "Malloc error in openFile\n");
        res = 0;
        SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
        exit(EXIT_FAILURE);
    }

    *newClient = fd;

    if(list_push(&(f->clients), newClient) == -1) {
        perror("list_push");
        res = -1;
        SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
        exit(EXIT_FAILURE);
    }
    Pthread_mutex_unlock(&mutex_storage);

    free(path);

    res = 0;
    SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
    
}

void closeConnection(int fd, int endpoint) {
    int numFiles;
    SYSCALL_ONE_EXIT(readn(fd, &numFiles, sizeof(int)), "readn")

    for(int i = 0; i < numFiles; i++) {
        int len;
        SYSCALL_ONE_EXIT(readn(fd, &len, sizeof(int)), "readn")
        
        char* path = calloc(len, sizeof(char));
        EQ_NULL_EXIT(path, "calloc in closeConnection")

        SYSCALL_ONE_EXIT(readn(fd, path, len * sizeof(char)), "readn")

        Pthread_mutex_lock(&mutex_storage);
        file_t *f = (file_t*) icl_hash_find(storage, path);
        if (f == NULL) {
            fprintf(stderr, "File non presente nello storage\n");
            int res = -1;
            SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
            exit(EXIT_FAILURE);
        }
        if (list_delete(&(f->clients), &fd, free) == -1) {
            perror("list_delete in closeConnection");
            int res = -1;
            SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
            exit(EXIT_FAILURE);
        }
        Pthread_mutex_unlock(&mutex_storage);

        free(path);
    }
    int res = 0;
    SYSCALL_ONE_EXIT(writen(fd, &res, sizeof(int)), "readn")
    int msg = -1;
    SYSCALL_ONE_EXIT(writen(endpoint, &msg, sizeof(int)), "readn")
    close(fd);
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
        SYSCALL_ONE_EXIT(readn(fd, &opt, sizeof(int)), "readn")

        switch (opt) {
            case FIND_FILE: findFile(fd); break;
            case CREATE_FILE: createFile(fd); break;
            case OPEN_FILE: openFile(fd); break;
            case END_CONNECTION: closeConnection(fd, Wendpoint); continue;
        }

        //Capire se si può migliorare
        if (opt == -1) {
            close(fd);
            fd = -1;
        }
        if (writen(Wendpoint, &fd, sizeof(int)) == -1) {
            perror("writen in thread worker");
            exit(EXIT_FAILURE);
        }
    }

    return NULL;
} //Il client invierà il messaggio di termine connessione e il thread chiuderà il FD.

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

                if (fd == fdPipe[0]) {
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
                    printf("Client connesso\n");
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

    // SYSCALL_ONE_EXIT(list_destroy(&fileHistory, free), "list_destroy") //Non serve perché fileHistory e la tabella hash hanno come chiave la stessa variabile allocata sullo heap
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