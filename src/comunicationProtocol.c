#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX 108
#endif

#if !defined(STRLEN)
#define STRLEN 256
#endif

static char* socketName;
static int fdSocket;

int openConnection(const char* sockname, int msec, const struct timespec abstime) {
    if (sockname == NULL) {
        errno = ENOTSOCK;
        return -1;
    }

    if ((fdSocket = socket(AF_UNIX, SOCK_STREAM,0)) == -1) return -1;

    struct sockaddr_un sa;
    strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;

    while(connect(fdSocket, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
        if (errno == ENOENT) { //Aggiungere l'abstime
            struct timespec nextTry;
            nextTry.tv_sec = msec / 1000;
            nextTry.tv_nsec = (msec % 1000) * 1000000;
            int res;
            do {
                res = nanosleep(&nextTry, &nextTry);
            } while(res && errno == EINTR);
        } else {
            return -1;
        }
    }

    return 0;
}

int closeConnection(const char* sockname) {
    if(strncmp(socketName, sockname, strnlen(socketName, STRLEN))) {
        errno = EINVAL;
        return -1;
    }

    close(fdSocket);
    return 0;
}