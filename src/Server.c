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

#define STRLEN 256
#define UNIX_PATH_MAX 108
#define MAXCONN 100

static pthread_mutex_t connections = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t emptyConnections = PTHREAD_COND_INITIALIZER;

int sigCaught = 0;
int stopRequests = 0;
int stopConnections = 0;

long max_space;
int max_file;
char* socketName;
char* logFile;

typedef struct _connection {
    int fd;
    struct _connection *next;
} connection_t;

connection_t *connectionBuffer = NULL;
connection_t *tailCBuff = NULL;

void freeGlobal(){
    if (socketName != NULL) free(socketName);
    if (logFile != NULL) free(logFile);
}

void* sighandler(void* arg){ //Da scrivere nella relazione: Si suppone che se fallisce la sigwait tutto il processo viene terminato perché potrei non riuscire più a terminare il server
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

int parseFile(const char* filepath, int* numWorkers, long* memorySpace, int* numFile, char** socketName, char** logFile) { //Migliorare il parsing controllando che la stringa sia effettivamente solo quella
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

            int len = strnlen(str, STRLEN);
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
            int len = strnlen(str + 12, STRLEN); //Sfrutto il fatto che conta \n per allocare lo spazio del \0
            *socketName = calloc(len, sizeof(char));
            if (*socketName == NULL) {
                perror("calloc");
                free(str);
                return -1;
            }
            
            strncpy(*socketName, str + 12, len);
        }

        if(strncmp(str, "LOG_FILE", 8) == 0) {
            int len = strnlen(str + 9, STRLEN); //Sfrutto il fatto che conta \n per allocare lo spazio del \0
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

void* workerThread(void* arg) {return NULL;} //Il client invierà il messaggio di termine connessione e il thread chiuderà il FD.

void initializeSocket(int* fd_socket) {
    *fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    SYSCALL_ONE_EXIT(*fd_socket, "socket")

    struct sockaddr_un sa;
    strncpy(sa.sun_path, socketName, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    
    SYSCALL_ONE_EXIT(bind(*fd_socket, (struct sockaddr *) &sa, sizeof(sa)), "bind")
    SYSCALL_ONE_EXIT(listen(*fd_socket, MAXCONN), "listen")
}

void spawnThread(int W, int writeEndpoint) {
    for(int i = 0; i < W; i++) {
        int err;
        pthread_t tid;
        SYSCALL_NOT_ZERO_EXIT(err, pthread_create(&tid, NULL, workerThread, (void*) &writeEndpoint), "pthread_create")

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

    spawnThread(numW, fdPipe[1]); //Aggiungere freeGlobal

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

                if (fd == fdPipe[0]) { //Bisogna aggiungere che il thread potrebbe rispondere che quella connessione è stata chiusa
                    int c_fd;

                    if (readn(fd, &c_fd, sizeof(int)) == -1) {
                        perror("read");
                        return errno;
                    }

                    FD_SET(c_fd, &set);
                    if (c_fd > fdMax) fdMax = c_fd;
                    continue;
                }

                if (fd == listenSocket && !stopConnections) { //Dopo la accept bisognerebbe controllare EINTR? No perché non si blocca mai
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

                if (fd != fdPipe[0] && fd != listenSocket) {
                    FD_CLR(fd, &set);
                    fdMax = updateSet(&set, fdMax);

                    connection_t *new = malloc(sizeof(connection_t));
                    EQ_NULL_EXIT(new, "malloc") //Aggiungere freeGlobal

                    new->fd = fd;
                    new->next = NULL;

                    Pthread_mutex_lock(&connections);
                    if (connectionBuffer == NULL) {
                        connectionBuffer = new;
                        tailCBuff = new;
                    } else {
                        tailCBuff->next = new;
                        tailCBuff = new;
                    }
                    pthread_cond_signal(&emptyConnections);
                    Pthread_mutex_unlock(&connections);
                }
            }
        }

        if (stopConnections) stillConnected = checkConnections(&set, fdMax, listenSocket, fdPipe[0], signalPipe[0]); //Ricontrollare

    }


    SYSCALL_NOT_ZERO_EXIT(err, pthread_join(sighandler_thread, NULL), "pthread_join")

    close(listenSocket); //Questo dovrebbe essere tolto
    close(fdPipe[0]);
    close(fdPipe[1]);
    close(signalPipe[0]);
    close(signalPipe[1]);
    SYSCALL_ONE_EXIT(unlink(socketName), "unlink");
    freeGlobal();

    return 0;
}