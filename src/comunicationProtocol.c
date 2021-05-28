#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


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

static char* socketName = NULL;
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

    if(list_create(&openedFiles, int_compare) == -1) return -1;

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

    //Invia il numero di file da chiudere al server
    if (writen(fdSocket, &(openedFiles.dim), sizeof(int)) == -1) return -1;

    int nFiles = openedFiles.dim;

    for(int i = 0; i < nFiles; i++) {
        char* path = list_pop(&openedFiles);
        if (path == NULL) return -1;

        int len = strnlen(path, STRLEN) + 1;
        if (writen(fdSocket, &len, sizeof(int)) == -1) return -1;
        if (writen(fdSocket, path, len * sizeof(char)) == -1) return -1;
        free(path);
    }
    
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
 * @return 0 in caso di successo, -1 in caso di fallimento (setta errno)
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

    if (res == 0) {
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


/*
* Scrive tutto il file puntato da pathname nel file server. Ritorna successo solo se la precedente operazione,
* terminata con successo, è stata openFile(pathname, O_CREATE| O_LOCK). Se ‘dirname’ è diverso da NULL, il
* file eventualmente spedito dal server perchè espulso dalla cache per far posto al file ‘pathname’ dovrà essere
* scritto in ‘dirname’; Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
*/
int writeFile(const char* pathname, const char* dirname);


/*
* Richiesta di scrivere in append al file ‘pathname‘ i ‘size‘ bytes contenuti nel buffer ‘buf’. L’operazione di append
* nel file è garantita essere atomica dal file server. Se ‘dirname’ è diverso da NULL, il file eventualmente spedito
* dal server perchè espulso dalla cache per far posto ai nuovi dati di ‘pathname’ dovrà essere scritto in ‘dirname’;
* Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
*/
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname) {
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (socketName == NULL) {
        errno = ENOTCONN;
        return -1;
    }

    //Copia il nome del file
    int pathlen = strnlen(pathname, STRLEN) + 1;
    char* tmp = calloc(pathlen, sizeof(char));
    if (tmp == NULL) return -1;
    strncpy(tmp, pathname, pathlen);

    if (list_find(&openedFiles, tmp) == NULL) {
        free(tmp);
        errno = ENOENT;
        return -1;
    }

    int exists;
    if ((exists = existFile(tmp)) == -1) {
        free(tmp);
        return -1;
    }

    if (!exists) {
        //Il file non è più presente nel server. Viene rimosso dalla lista dei file aperti e viene restituito un errore
        if (list_delete(&openedFiles, tmp, free) == -1) {free(tmp); return -1;}
        free(tmp); 
        errno = ENOENT;
        return -1;
    }

    //Richiesta al server
    int opt = APPEND_FILE;
    if (writen(fdSocket, &opt, sizeof(int)) == -1) {free(tmp); return -1;}
    if (writen(fdSocket, &pathlen, sizeof(int)) == -1) {free(tmp); return -1;}
    if (writen(fdSocket, &tmp, pathlen * sizeof(char)) == -1) {free(tmp); return -1;}
    if (writen(fdSocket, &size, sizeof(size_t)) == -1) {free(tmp); return -1;}
    if (writen(fdSocket, buf, size * sizeof(void)) == -1) {free(tmp); return -1;}

    free(tmp);

    //Lettura risposta
    int res;
    if (readn(fdSocket, &res, sizeof(int)) == -1) return -1;

    if (res != SEND_FILE) return res; //Bisogna settare errno
    
    if (dirname != NULL) {
        //Verifica che il dirname sia una directory
        struct stat info;
        if (stat(dirname, &info) == -1) return -1;
        if (!S_ISDIR(info.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
    }
    
    while(res == SEND_FILE) {
        //Lettura della lunghezza del path
        int len;
        if (readn(fdSocket, &len, sizeof(int)) == -1) return -1;

        char* path = calloc(len, sizeof(char));
        if (path == NULL) return -1; //Settare errno

        //Lettura del path
        if (readn(fdSocket, path, len * sizeof(char)) == -1) {free(path); return -1;}

        //Lettura della dimensione del file
        size_t dim;
        if (readn(fdSocket, &dim, sizeof(size_t)) == -1) {free(path); return -1;}

        void* data = malloc(dim * sizeof(void));
        if (data == NULL) {free(path); return -1;} //Settare errno

        //Lettura del file
        if (readn(fdSocket, data, dim * sizeof(void)) == -1) {free(path); return -1;}

        if (dirname == NULL) { //Se non è stata specificata la directory, i file vengono scartati
            free(path);
            free(data);
            if (readn(fdSocket, &res, sizeof(int)) == -1) return -1;
            continue;
        }

        //Manipolazione delle stringhe per estrapolare dal path il nome e l'eventuale estensione del file
        int startName;
        int fullstop = -1;
        for(startName = len - 1; startName >= 0; startName--) {
            if (path[startName] == '/') break;
            if (path[startName] == '.') fullstop = fullstop == -1 ? startName : fullstop;
        }
        startName++;

        char* extension;
        if (fullstop != -1) {
            extension = calloc(len - fullstop, sizeof(char));
            if (extension == NULL) {
                free(path);
                free(data);
                return -1;
            }

            strncpy(extension, path + fullstop, len - fullstop);
        }

        //Salvataggio e cambio della CWD per poter salvare i file
        char* cwd = calloc(512, sizeof(char));
        if (cwd == NULL) {
            free(path);
            free(data);
            if (fullstop != -1) free(extension);
            return -1;
        }

        if ((cwd = getcwd(cwd, 512)) == NULL) {
            free(path);
            free(data);
            if (fullstop != -1) free(extension);
            free(cwd);
            return -1; //Bisogna controllare se errno == ERANGE e fare una realloc e riprovare
        }

        if (chdir(dirname) == -1) {
            free(path);
            free(data);
            if (fullstop != -1) free(extension);
            free(cwd);
            return -1;
        }

        //Creazione del file. Se esiste un file con lo stesso nome verrà modificato il nome del file da creare
        int createdFile, oldCifre;
        int try = 1;
        while((createdFile = open(path + startName, O_WRONLY | O_CREAT | O_EXCL)) == -1) {
            if (errno != EEXIST) return -1;

            if (try == 1) {
                char* tmp = realloc(path, (len + 3) * sizeof(char));
                if (tmp == NULL) return -1;
                path = tmp;
                len += 3;
            }

            int tmp_try = try;
            int nCifre = 0;
            while (tmp_try != 0) {
                tmp_try /= 10;
                nCifre++;
            }

            if (nCifre > oldCifre) {
                char* tmp = realloc(path, (len + 1) * sizeof(char));
                if (tmp == NULL) return -1;
                path = tmp;
                len++;
            }

            oldCifre = nCifre;

            snprintf(path + fullstop, sizeof(int) + 2 * sizeof(char), "(%d)", try);
            if (fullstop != -1) strncpy(path + fullstop + nCifre + 2, extension, len - fullstop);
            
            try++;
        }

        //Scrittura nel file
        if (writen(createdFile, data, dim * sizeof(void)) == -1) {
            free(path);
            free(data);
            if (fullstop != -1) free(extension);
            free(cwd);
            return -1;
        }

        //Chiusura del file
        close(createdFile);

        //Ripristino della precedente CWD
        if (chdir(cwd) == -1) {
            free(path);
            free(data);
            if (fullstop != -1) free(extension);
            free(cwd);
            return -1;
        }

        //Libera la memoria
        if (fullstop != -1) free(extension);
        free(cwd);
        free(path);
        free(data);

        //Lettura del prossimo potenziale file inviato dal server
        if (readn(fdSocket, &res, sizeof(int)) == -1) return -1;
    }

    return 0;
}


/*
* Richiesta di chiusura del file puntato da ‘pathname’. Eventuali operazioni sul file dopo la closeFile falliscono.
* Ritorna 0 in caso di successo, -1 in caso di fallimento, errno viene settato opportunamente.
*/
int closeFile(const char* pathname) {
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (socketName == NULL) {
        errno = ENOTCONN;
        return -1;
    }

    //Copia il nome del file
    int pathlen = strnlen(pathname, STRLEN) + 1;
    char* tmp = calloc(pathlen, sizeof(char));
    if (tmp == NULL) return -1;
    strncpy(tmp, pathname, pathlen);

    if (list_find(&openedFiles, tmp) == NULL) {
        free(tmp);
        errno = ENOENT;
        return -1;
    }

    if (list_delete(&openedFiles, tmp, free) == -1) {
        free(tmp);
        return -1;
    }

    int exists;
    if ((exists = existFile(tmp)) == -1) {
        free(tmp);
        return -1;
    }

    if (!exists) {
        free(tmp);
        errno = ENOENT;
        return -1;
    }

    int opt = CLOSE_FILE;

    if(writen(fdSocket, &opt, sizeof(int)) == -1) {
        free(tmp);
        return -1;
    }

    if (writen(fdSocket, &pathlen, sizeof(int)) == -1) {
        free(tmp);
        return -1;
    }
    if (writen(fdSocket, tmp, pathlen * sizeof(char)) == -1) {
        free(tmp);
        return -1;
    }
    free(tmp);

    //Attende la risposta del server
    int res;
    if (readn(fdSocket, &res, sizeof(int)) == -1) return -1;
    
    if (res == -1) errno = ENOENT;
    return res;
}