#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "../includes/util.h"

int sigCaught = 0;

void* sighandler(void* arg){ //Da scrivere nella relazione: Si suppone che se fallisce la sigwait tutto il processo viene terminato perché potrei non riuscire più a terminare il server
    sigset_t mask = *(sigset_t*) arg;
    
    while(1) {
        int sig;
        int err;
        SYSCALL_NOT_ZERO_EXIT(err, sigwait(&mask, &sig), "sigwait")

        sigCaught = 1;
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
    SYSCALL_NOT_ONE_EXIT(sigaction(SIGPIPE, &s, NULL), "sigaction")

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





    SYSCALL_NOT_ZERO_EXIT(err, pthread_join(sighandler_thread, NULL), "pthread_join")
    return 0;
}