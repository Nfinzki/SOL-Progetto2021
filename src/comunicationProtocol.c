#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "../includes/comunication.h"

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX 108
#endif

#if !defined(STRLEN)
#define STRLEN 256
#endif

static char* socketName;
static int fdSocket;

typedef struct _openedFile {
    char* pathname;
    struct _openedFile *next;
} openedFile_t;

static openedFile_t *head = NULL;

/* Viene aperta una connessione AF_UNIX al socket file sockname. Se il server non accetta immediatamente la
* richiesta di connessione, la connessione da parte del client viene ripetuta dopo ‘msec’ millisecondi e fino allo
* scadere del tempo assoluto ‘abstime’ specificato come terzo argomento. Ritorna 0 in caso di successo, -1 in caso
* di fallimento, errno viene settato opportunamente.
*/
int openConnection(const char* sockname, int msec, const struct timespec abstime) {
    if (sockname == NULL) {
        errno = ENOTSOCK;
        return -1;
    }

    if ((fdSocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) return -1;

    struct sockaddr_un sa;
    strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;

    while(connect(fdSocket, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
        int err = errno;

        struct timespec actualTime;
        if (clock_gettime(CLOCK_REALTIME, &actualTime) == -1) return -1;

        errno = err;

        int keepTrying = 1;
        if (actualTime.tv_sec > abstime.tv_sec)
            keepTrying = 0;
        if (actualTime.tv_sec == abstime.tv_sec && actualTime.tv_nsec > abstime.tv_nsec) keepTrying = 0;

        if (errno == ENOENT && keepTrying) {
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


/* Chiude la connessione AF_UNIX associata al socket file sockname. Ritorna 0 in caso di successo, -1 in caso di
* fallimento, errno viene settato opportunamente.
*/
int closeConnection(const char* sockname) {
    if(strncmp(socketName, sockname, strnlen(socketName, STRLEN))) {
        errno = EINVAL;
        return -1;
    }

    close(fdSocket);
    return 0;
}


/* Richiesta di apertura o di creazione di un file. La semantica della openFile dipende dai flags passati come secondo
* argomento che possono essere O_CREATE ed O_LOCK. Se viene passato il flag O_CREATE ed il file esiste già
* memorizzato nel server, oppure il file non esiste ed il flag O_CREATE non è stato specificato, viene ritornato un
* errore. In caso di successo, il file viene sempre aperto in lettura e scrittura, ed in particolare le scritture possono
* avvenire solo in append. Se viene passato il flag O_LOCK (eventualmente in OR con O_CREATE) il file viene
* aperto e/o creato in modalità locked, che vuol dire che l’unico che può leggere o scrivere il file ‘pathname’ è il
* processo che lo ha aperto. Il flag O_LOCK può essere esplicitamente resettato utilizzando la chiamata unlockFile,
* descritta di seguito.
* Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
*/
int openFile(const char* pathname, int flags) {
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }


}