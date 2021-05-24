#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "../includes/comunication.h"
#include "../includes/comunicationOptions.h"
#include "../includes/comunicationProtocol.h"
#include "../includes/list.h"

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX 108
#endif

#if !defined(STRLEN)
#define STRLEN 256
#endif

static char* socketName;
static int fdSocket;

static list_t openedFiles;

/* Viene aperta una connessione AF_UNIX al socket file sockname. Se il server non accetta immediatamente la
* richiesta di connessione, la connessione da parte del client viene ripetuta dopo ‘msec’ millisecondi e fino allo
* scadere del tempo assoluto ‘abstime’ specificato come terzo argomento. Ritorna 0 in caso di successo, -1 in caso
* di fallimento, errno viene settato opportunamente.
*/
int openConnection(const char* sockname, int msec, const struct timespec abstime) { //Cosa succede se si fa una seconda openConnection senza aver chiuso quella precedente?
    if (sockname == NULL) {
        errno = ENOTSOCK;
        return -1;
    }

    int socklen = strnlen(sockname, STRLEN);
    if((socketName = calloc(socklen, sizeof(char))) == NULL) return -1;
    
    strncpy(socketName, sockname, socklen);

    if ((fdSocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        free(socketName);
        return -1;
    }

    struct sockaddr_un sa;
    strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;

    while(connect(fdSocket, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
        int err = errno;

        struct timespec actualTime;
        if (clock_gettime(CLOCK_REALTIME, &actualTime) == -1) {
            free(socketName);
            return -1;
        }

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
            free(socketName);
            return -1;
        }
    }

    list_create(&openedFiles);

    return 0;
}


/* Chiude la connessione AF_UNIX associata al socket file sockname. Ritorna 0 in caso di successo, -1 in caso di
* fallimento, errno viene settato opportunamente.
*/
int closeConnection(const char* sockname) { //I file aperti dovrebbero venire chiusi
    if(strncmp(socketName, sockname, strnlen(socketName, STRLEN))) {
        errno = EINVAL;
        return -1;
    }

    int opt = END_CONNECTION;
    if (writen(fdSocket, &opt, sizeof(int)) == -1) return -1;
    
    int res;
    if (readn(fdSocket, &res, sizeof(int)) == -1) return -1;

    if (res != 0) {
        errno = ECONNABORTED;
        close(fdSocket);
        free(socketName);
    }

    close(fdSocket);
    free(socketName);
    return 0;
}


/** Richiesta dell'esistenza di un file.
 * @param pathname -- file da cercare
 * 
 * @return 0 se il file non esiste, 1 se esiste, -1 in caso di errore (setta errno)
**/
static int existFile(const char* pathname) {
    int pathlen = strnlen(pathname, STRLEN) + 1;
    char* tmp = calloc(pathlen, sizeof(char));
    if (tmp == NULL) return -1;
    strncpy(tmp, pathname, pathlen);

    // Invia il tipo di operazione
    // Invia la dimensione del messaggio
    // Invia il messaggio
    int opt = FIND_FILE;
    if (writen(fdSocket, &opt, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }
    // int len_msg = strnlen(pathname, STRLEN);
    if (writen(fdSocket, &pathlen, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }
    if (writen(fdSocket, tmp, pathlen * sizeof(char)) == -1) {
        return -1;
        free(tmp);
    }

    //Lettura risposta dal server
    int exists;
    if (readn(fdSocket, &exists, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }

    free(tmp);
    return exists; //Fare una migliore gestione dell'errore
}


/** Richiesta di creazione di un file.
 * @param pathname -- file da creare
 * 
 * @return 0 in caso di cuccesso, -1 in caso di fallimento (setta errno)
**/
static int createFile(const char* pathname) {
    int pathlen = strnlen(pathname, STRLEN) + 1;
    char* tmp = calloc(pathlen, sizeof(char));
    if (tmp == NULL) return -1;
    strncpy(tmp, pathname, pathlen);

    int opt = CREATE_FILE;
    if (writen(fdSocket, &opt, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }
    // int len_msg = strnlen(pathname, STRLEN);
    if (writen(fdSocket, &pathlen, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }
    if (writen(fdSocket, tmp, pathlen * sizeof(char)) == -1) {
        return -1;
        free(tmp);
    }

    //Lettura risposta dal server
    int res;
    if (readn(fdSocket, &res, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }

    free(tmp);
    return res; //Fare una migliore gestione dell'errore
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
    if (socketName == NULL) {
        errno = ENOTCONN;
        return -1;
    }
    
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (flags == O_LOCK || flags == (O_CREATE | O_LOCK)) {
        errno = ENOLCK;
        return -1;
    }

    int exists;
    if((exists = existFile(pathname)) == -1) return -1;

    //File esistente e flag di creazione attivo
    if (flags == O_CREATE && exists) {
        errno = EEXIST;
        return -1;
    }

    //File non esistente e flag di creazione non attivo
    if (!exists && flags != O_CREATE) {
        errno = EADDRNOTAVAIL;
        return -1;
    }


    if (flags == O_CREATE) { //Crea il file
        if (createFile(pathname) == -1) {
            errno = EACCES; //Da cambiare
            return -1;
        }
    }

    //Copia il nome del file
    int pathlen = strnlen(pathname, STRLEN) + 1;
    char* tmp = calloc(pathlen, sizeof(char));
    if (tmp == NULL) return -1;
    strncpy(tmp, pathname, pathlen);

    //Apre il file
    int opt = OPEN_FILE;
    if (writen(fdSocket, &opt, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }
    // int len_msg;
    if (writen(fdSocket, &pathlen, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }
    if (writen(fdSocket, tmp, pathlen * sizeof(char)) == -1) {
        return -1;
        free(tmp);
    }

    //Risposta del server
    int res;
    if (readn(fdSocket, &res, sizeof(int)) == -1) {
        return -1;
        free(tmp);
    }

    if (res) {
        if(list_append(&openedFiles, tmp) == -1) {
            return -1;
            free(tmp);
        }
    }

    return res;
}


/* Legge tutto il contenuto del file dal server (se esiste) ritornando un puntatore ad un'area allocata sullo heap nel
* parametro ‘buf’, mentre ‘size’ conterrà la dimensione del buffer dati (ossia la dimensione in bytes del file letto). In
* caso di errore, ‘buf‘e ‘size’ non sono validi. Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene
* settato opportunamente
*/
int readFile(const char* pathname, void** buf, size_t* size);