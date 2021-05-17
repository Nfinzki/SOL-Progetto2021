#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include "../includes/util.h"

#define STRLEN 256

int sigCaught = 0;

int max_space;
int max_file;
char* socketName;
char* logFile;

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
        return EXIT_FAILURE;
    }


    SYSCALL_NOT_ZERO_EXIT(err, pthread_join(sighandler_thread, NULL), "pthread_join")
    return 0;
}