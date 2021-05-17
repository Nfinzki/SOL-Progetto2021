#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#include "../includes/util.h"

#define STRLEN 256
#define UNIX_PATH_MAX 108
#define MAXCONN 100

static pthread_mutex_t connections = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t emptyConnections = PTHREAD_COND_INITIALIZER;

int sigCaught = 0;

int max_space;
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
    sigset_t mask = *(sigset_t*) arg;
    
    while(1) {
        int sig;
        int err;
        SYSCALL_NOT_ZERO_EXIT(err, sigwait(&mask, &sig), "sigwait")

        sigCaught = 1;
        printf("Segnale catturato: %d\n", sig);
        fflush(stdout);
        return NULL;
    }
    return NULL;
}

void setHandlers(sigset_t *mask) {
    //Maschero i segnali SIGINT, SIGQUIT, SIGHUP che verranno gestiti dal thread handler
    int err;
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigaddset(mask, SIGQUIT);
    sigaddset(mask, SIGHUP);
    SYSCALL_NOT_ZERO_EXIT(err, pthread_sigmask(SIG_BLOCK, mask, NULL), "pthread_sigmask")

    //Ignoro SIGPIPE
    struct sigaction s;
    memset(&s, 0, sizeof(s));
    s.sa_handler = SIG_IGN;
    SYSCALL_ONE_EXIT(sigaction(SIGPIPE, &s, NULL), "sigaction")

}

int parseFile(const char* filepath, int* numWorkers, int* memorySpace, int* numFile, char** socketName, char** logFile) { //Migliorare il parsing controllando che la stringa sia effettivamente solo quella
    FILE *fd;
    if((fd = fopen(filepath, "r")) == NULL) {
        perror("fopen");
        exit(errno);
    }

    char* str = calloc(STRLEN, sizeof(char));
    EQ_NULL_RETURN(str, "calloc")

    while(fgets(str, STRLEN, fd) != NULL) {
        if (str[strnlen(str, STRLEN) - 1] == '\n') str[strnlen(str, STRLEN) - 1] = '\0';

        if(strncmp(str, "N_WORKERS", 9) == 0){
            long num;
            if(isNumber(str + 10, &num) != 0) { //Magari aggiungere la discriminazione tra i vari errori (anche a quelli dopo)
                fprintf(stderr, "Error in isNumber1\n");
                free(str);
                return -1;
            }

            *numWorkers = num;
        }

        if(strncmp(str, "MEM_SPACE", 9) == 0){
            long num;
            if(isNumber(str + 10, &num) != 0) {
                fprintf(stderr, "Error in isNumber2\n");
                free(str);
                return -1;
            }

            *memorySpace = num;
        }

        if(strncmp(str, "FILE_SPACE", 10) == 0) {
            long num;
            if(isNumber(str + 11, &num) != 0) {
                fprintf(stderr, "Error in isNumber3\n");
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
    return 0;
}

void* workerThread(void* arg) {}

void inizializeSocket(int* fd_socket) {
    *fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    SYSCALL_ONE_EXIT(*fd_socket, "socket")

    struct sockaddr_un sa;
    strncpy(sa.sun_path, socketName, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;
    
    SYSCALL_ONE_EXIT(bind(*fd_socket, (struct sockaddr *) &sa, sizeof(sa)), "bind")
    SYSCALL_ONE_EXIT(listen(*fd_socket, MAXCONN), "listen")
}

void spawnThread(int W) {
    for(int i = 0; i < W; i++) {
        int err;
        pthread_t tid;
        SYSCALL_NOT_ZERO_EXIT(err, pthread_create(&tid, NULL, workerThread, NULL), "pthread_create")

        SYSCALL_NOT_ZERO_EXIT(err, pthread_detach(tid), "pthread_detach")
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Invalid number of arguments...\n");
        return EXIT_FAILURE;
    }

    sigset_t mask;
    setHandlers(&mask);

    int err;
    pthread_t sighandler_thread;
    SYSCALL_NOT_ZERO_EXIT(err, pthread_create(&sighandler_thread, NULL, sighandler, (void*) &mask), "pthread_create")

    int numW;
    if (parseFile(argv[1], &numW, &max_space, &max_file, &socketName, &logFile) != 0) {
        fprintf(stderr, "Error parsing %s\n", argv[1]);
        freeGlobal();
        return EXIT_FAILURE;
    }

    spawnThread(numW); //Aggiungere freeGlobal

    int listenSocket;
    initializeSocket(&listenSocket); //Aggiungere freeGlobal

    int fdMax = 0;
    if (listenSocket > fdMax) fdMax = listenSocket;
    fd_set set, rdset;
    FD_ZERO(&set);
    FD_SET(listenSocket, &set);
    
    while(!sigCaught) {
        rdset = set;

        if (select(fdMax + 1, &rdset, NULL, NULL, NULL) == -1) {
            if (errno == EINTR && sigCaught) break;
            perror("select");
            freeGlobal();
            return -1;
        }

        for(int fd = 0; fd < fdMax + 1; fd++) {
            if (FD_ISSET(fd, &rdset)) {
                if (fd == listenSocket) { //Dopo la accept bisognerebbe controllare EINTR e poi controllare se bisogna mettere newFd nel set
                    int newFd;
                    newFd = accept(listenSocket, NULL, 0);
                    
                    if (newFd == -1) {
                        fprintf(stderr, "An error has occurred accepting connection\n");
                        continue;
                    }

                    connection_t *new = malloc(sizeof(connection_t));
                    EQ_NULL_EXIT(new, "malloc") //Aggiungere freeGlobal

                    new->fd = newFd;
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
    }


    SYSCALL_NOT_ZERO_EXIT(err, pthread_join(sighandler_thread, NULL), "pthread_join")
    freeGlobal();
    return 0;
}