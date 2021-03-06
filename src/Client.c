#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>

#include "../includes/util.h"
#include "../includes/comunicationProtocol.h"
#include "../includes/list.h"
#include "../includes/comunication.h"
#include "../includes/comunicationFlags.h"

#define STRLEN 512

typedef struct _request {
    char flag;
    int option;
    char** arg; //Lista dei file/directory
    int dim; //Dimensione della lista
} request_t;

list_t* requestLst; //Lista delle richieste da effettuare
list_t* dirLst; //Lista delle directory specificate da '-d' in cui salvare i file
list_t* DdirLst; //Lista delle directory specificate da '-D' in cui salvare i file

int flagP = 0;

//Confronta due richieste
int compareRequest(void* a, void* b) {
    request_t *reqA = (request_t*) a;
    request_t *reqB = (request_t*) b;

    if (reqA->flag != reqB->flag || reqA->option != reqB->option || reqA->dim != reqB->dim) return 0;
    for(int i = 0; i < reqA->dim; i++)
        if (strncmp(reqA->arg[i], reqB->arg[i], STRLEN) != 0) return 0;
    return 1;
}

//Libera la memoria di una richiesta
void freeRequest(void* r) {
    request_t *req = (request_t*) r;
    for(int i = 0; i < req->dim; i++)
        free(req->arg[i]);
    free(req->arg);
    free(req);
}

int countComma (char* str) {
    int len = strnlen(str, STRLEN);
    int commas = 0;
    for(int i = 0; i < len; i++) {
        if (str[i] == ',') commas++;
    }
    return commas;
}

//Separa tutti i file contenuti nella stringa
char** tokString (char** str, int* initSize) {
    *initSize = countComma(*str) + 1;

    char** arg = malloc(sizeof(char*) * (*initSize));

    if (arg == NULL) {
        perror("malloc");
        return NULL;
    }

    char *state = NULL;
    char *token = strtok_r(*str, ",", &state);

    int i = 0;
    while(token) {
        arg[i] = realpath(token, NULL);
        if (arg[i] == NULL) {
            perror("realpath");
            return NULL;
        }
        i++;
        token = strtok_r(NULL, ",", &state);
    }

    return arg;
}

void arg_h (const char* name) {
    printf("usage: %s -f <filename> -w <dirname>[,n=0] -W <file1>[,file2] -D <dirname> -r <file1>[,file2] -R [n=0] -d <dirname> -t <time> -l <file1>[,file2] -u <file1>[,file2] -c <file1>[,file2] -p -h\n", name);
}

void arg_w (char* arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_w");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'w';
    newR->dim = 1;

    int lenDir = -1;
    for(int i = 0; i < strlen(arg); i++) {
        if(arg[i] == ',') {
            arg[i] = '\0';
            lenDir = i + 1;
            break;
        }
    }

    newR->arg = malloc(sizeof(char*));
    if (newR->arg == NULL) {
        perror("malloc in arg in arg_w");
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    newR->arg[0] = calloc(STRLEN, sizeof(char));
    if(newR->arg[0] == NULL) {
        perror("malloc in arg[0] in arg_w");
        free(newR->arg);
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    strncpy(newR->arg[0], arg, strlen(arg));

    if (lenDir == -1) {
        newR->option = 0;
    } else {
        long tmp;

        //Converte l'argomento opzionale in numero
        if (isNumber(arg + lenDir + 2, &tmp) != 0) {
            fprintf(stderr, "Errore in isNumber in arg_w\n");
            freeRequest(newR);
            SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
            exit(EXIT_FAILURE);
        }
        newR->option = tmp;
    }

    //Aggiunge la richiesta in coda
    if (list_append(requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
    }
}

void arg_W(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_W");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'W';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    //Aggiunge la richiesta in coda
    if (list_append(requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
    }
}

void arg_r(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_r");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'r';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    //Aggiunge la richiesta in coda
    if (list_append(requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
    }
}

int arg_R(char* arg) {
    if (arg[0] != 'n' || arg[1] != '=') {
        fprintf(stderr, "Formato non valido per il flag -R\n");
        return -1;
    }

    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_R");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy")
        exit(EXIT_FAILURE);
    }
    newR->flag = 'R';
    newR->dim = 0;
    newR->arg = NULL;

    //Converte l'argomento in numero
    long tmp;
    if (isNumber(arg+2, &tmp) != 0) {
        fprintf(stderr, "isNumber in case 'R'\n");
        free(newR);
        return -1;
    }
    newR->option = tmp;

    //Aggiunge la richiesta in coda
    if (list_append(requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        return -1;
    }

    return 1;
}

void arg_c(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_c");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'c';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    //Aggiunge la richiesta in coda
    if (list_append(requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
}

void arg_d(char* arg) {    
    int len = strnlen(arg, STRLEN) + 1;
    char* newD = calloc(len, sizeof(char));
    if(newD == NULL) {
        perror("calloc in arg_d");
        free(newD);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(dirLst, free), "list_destroy");
        exit(EXIT_FAILURE);
    }

    strncpy(newD, arg, len);

    //Aggiunge la richiesta in coda
    if (list_append(dirLst, newD) == -1) {
        free(newD);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(dirLst, free), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(DdirLst, free), "list_destroy");
        exit(EXIT_FAILURE);
    }
}

void arg_D(char* arg) {    
    char* newD = realpath(arg, NULL);
    if (newD == NULL) {
        perror("realpath");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(dirLst, free), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(DdirLst, free), "list_destroy");
        exit(EXIT_FAILURE);
    }

    //Aggiunge la richiesta in coda
    if (list_append(DdirLst, newD) == -1) {
        free(newD);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(dirLst, free), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(DdirLst, free), "list_destroy");
        exit(EXIT_FAILURE);
    }
}

void arg_l(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_l");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'l';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    //Aggiunge la richiesta in coda
    if (list_append(requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
}

void arg_u(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_c");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'u';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    //Aggiunge la richiesta in coda
    if (list_append(requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
}

void ignoreSigpipe() {
    struct sigaction s;
    //Ignora SIGPIPE
    memset(&s, 0, sizeof(s));
    s.sa_handler = SIG_IGN;
    SYSCALL_ONE_EXIT(sigaction(SIGPIPE, &s, NULL), "sigaction");
}

//Visita ricorsivamente le directory fino a quando non manda 'n' file al server
int inspectDir(const char* dir, int* n, char* saveDir) {
    if (*n == 0) return 0;

    DIR *d;
    struct dirent* currentFile;

    if ((d = opendir(dir)) == NULL) {
        perror("Opening directory");
        return -1;
    }

    //Salva il cwd
    char* cwd = calloc(STRLEN, sizeof(char));
    if (cwd == NULL) {
        perror("calloc in inspectDir");
        return -1;
    }

    //Nell'evenutlit?? che non ci sia abbastanza memoria allocata, la rialloca
    int len = STRLEN;
    if((cwd = getcwd(cwd, len)) == NULL) {
        if (errno != ERANGE) {perror("getcwd"); return -1;}
        do {
            len *= 2;
            char* tmp = realloc(cwd, len);
            if (tmp == NULL) {perror("realloc in inspectDir"); return -1;}
            cwd = tmp;
        } while((cwd = getcwd(cwd, len)) == NULL);
    }

    //Sposta la cwd in quella della cartella in analisi
    if (chdir(dir) == -1) {perror("chdir"); return -1;}

    while ((errno = 0, currentFile = readdir(d)) != NULL) {

        if (strncmp(".", currentFile->d_name, 2) != 0 && strncmp("..", currentFile->d_name, 3) != 0 && *n != 0) { //Se la directory ?? diversa da "." e ".."

            //Calcola il path assoluto
            char* filepath = realpath(currentFile->d_name, NULL);
            if (filepath == NULL) {
                perror("filepath");
                return -1;
            }

            struct stat statFile;
            if (stat(filepath, &statFile) == -1) {
                perror("During stat");
                return -1;
            }

            if (S_ISDIR(statFile.st_mode)) {
                //Visita ricorsiva della directory
                if (inspectDir(filepath, n, saveDir) == -1) {free(filepath); return -1;}
            } else {
                //Crea il file sul server
                if (flagP) printf("Apertura del file %s nel server\n", filepath);
                if (openFile(filepath, O_CREATE | O_LOCK) == -1) {perror("openFile"); return -1;}
                if (flagP) printf("Successo\n");

                //Scrive il file sul server
                if (flagP) printf("Scrittura del file %s nel server\n", filepath);
                if (writeFile(filepath, saveDir) == -1) {perror("writeFile"); return -1;}
                if (flagP) printf("Scrittura completata con successo\n");
                
                //Chiude il file
                if (flagP) printf("Chiusura del file %s\n", filepath);
                if (closeFile(filepath) == -1) {perror("closeFile"); return -1;}
                if (flagP) printf("Successo\n");

                (*n)--;
            }
            free(filepath);
        }
    }

    if (errno != 0) {
        perror("Error reading directory");
        return -1;
    }

    //Ripristina la CWD
    if (chdir(cwd) == -1) {perror("chdir"); return -1;}
    free(cwd);

    //Chiude la directory
    if (closedir(d) == -1) {perror("Closing directory"); return -1;}

    return 0;
}

//Implementa l'esecuzione della richiesta '-w'
int req_w(const char* dirname, int n, char* saveDir) {
    if (dirname == NULL) return -1;

    //Verifica che dirname sia una directory
    struct stat info;
    if (stat(dirname, &info) == -1) return -1;
    if (!S_ISDIR(info.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    if (n == 0) {
        if (flagP) printf("Scrittura di tutti i file nella directory %s nel server\n", dirname);
        n = -1;
    } else {
        if (flagP) printf("Scrittura di %d file dalla directory %s nel server\n", n, dirname);
    }
    if (inspectDir(dirname, &n, saveDir) == -1) {perror("inspectDir"); return -1;}
    if (flagP) printf("Scritture completate con successo\n");

    return 0;
}

//Crea un file nella directory dirname. Restituisce il file descriptor del file creato
int createLocalFile(char* dirname, char* filepath) {
    int len = strnlen(filepath, STRLEN) + 1;

    char* path = calloc(len, sizeof(char));
    if (path == NULL) return -1;
    strncpy(path, filepath, len);

    //Manipolazione delle stringhe per estrapolare dal path il nome e l'eventuale estensione del file
    int startName;
    int fullstop = -1;
    for(startName = len - 1; startName >= 0; startName--) {
        if (path[startName] == '/') break;
        if (path[startName] == '.') fullstop = (fullstop == -1 ? startName : fullstop);
    }
    startName++;

    char* extension;
    int extension_len = -1;
    if (fullstop != -1) {
        //Salvataggio estensione
        extension_len = len - fullstop;
        extension = calloc(extension_len, sizeof(char));
        if (extension == NULL) {free(path); return -1;}

        strncpy(extension, path + fullstop, extension_len);
    }

    char* cwd = calloc(STRLEN, sizeof(char));
    if (cwd == NULL) {
        fprintf(stderr, "Errore critico in memoria\n");
        free(path);
        if (fullstop != -1) free(extension);
        return -1;
    }

    //Salvataggio della CWD per poter salvare i file
    //Nell'evenutlit?? che non ci sia abbastanza memoria allocata, la rialloca
    int len_cwd = STRLEN;
    if((cwd = getcwd(cwd, len_cwd)) == NULL) {
        if (errno != ERANGE) {
            perror("getcwd"); 
            free(path);
            if (fullstop != -1) free(extension);
            free(cwd);
            return -1;
        }
        do {
            if (errno != ERANGE) {
                perror("getcwd"); 
                free(path);
                if (fullstop != -1) free(extension);
                free(cwd);
                return -1;
            } else {
                len_cwd *= 2;
                char* tmp = realloc(cwd, len_cwd);
                if (tmp == NULL) {
                    perror("realloc in req_W");
                    free(path);
                    if (fullstop != -1) free(extension);
                    free(cwd);
                    return -1;
                }
                cwd = tmp;
            }
        } while((cwd = getcwd(cwd, len_cwd)) == NULL);
    }

    //Cambio della CWD
    if (chdir(dirname) == -1) {
        if (fullstop != -1) free(extension);
        free(cwd);
        free(path);
        return -1;
    }

    //Creazione del file. Se esiste un file con lo stesso nome verr?? modificato il nome del file da creare
    int createdFile;
    int oldCifre = 0;
    int try = 1;
    while((createdFile = open(path + startName, O_WRONLY | O_CREAT | O_EXCL, 0666)) == -1) {
        if (errno != EEXIST) return -1;

        if (try == 1) {
            char* tmp = realloc(path, (len + 2) * sizeof(char));
            if (tmp == NULL) {
                fprintf(stderr, "realloc: Errore critico in memoria\n");
                free(path);
                if (fullstop != -1) free(extension);
                free(cwd);
                return -1;
            }
            path = tmp;
            len += 2;
        }

        int tmp_try = try;
        int nCifre = 0;
        while (tmp_try != 0) {
            tmp_try /= 10;
            nCifre++;
        }

        if (nCifre > oldCifre) {
            char* tmp = realloc(path, (len + 1) * sizeof(char));
            if (tmp == NULL) {
                fprintf(stderr, "realloc: Errore critico in memoria\n");
                free(path);
                if (fullstop != -1) free(extension);
                free(cwd);
                return -1;
            }
            path = tmp;
            len++;
        }

        oldCifre = nCifre;

        //Modifica il nome del file per il nuovo tentativo
        int offset = (fullstop == -1 ? len-(3+nCifre) : fullstop);
        snprintf(path + offset, 2 + nCifre + 1, "(%d)", try);
        if (fullstop != -1) strncat(path, extension, extension_len);
        
        try++;
    }
    
    //Ripristino la vecchia CWD
    if (chdir(cwd) == -1) {
        free(cwd);
        free(path);
        if (fullstop != -1) free(extension);
        return -1;
    }

    //Libera la memoria
    free(cwd);
    free(path);
    if (fullstop != -1) free(extension);

    return createdFile;
}

//Implementa l'esecuzione della richiesta '-W'
int req_W(const char* path, char* saveDir) {
    if (path == NULL) return -1;

    if (flagP) printf("Apertura del file %s nel server\n", path);
    if (openFile(path, O_CREATE | O_LOCK) == -1) {perror("openFile"); return -1;}
    if (flagP) printf("Successo\n");

    if (flagP) printf("Scrittura del file %s nel server\n", path);
    if (writeFile(path, saveDir) == -1) {perror("writeFile"); return -1;}
    if (flagP) printf("Scrittura completata con successo\n");
    
    if (flagP) printf("Chiusura del file %s\n", path);
    if (closeFile(path) == -1) {perror("closeFile"); return -1;}
    if (flagP) printf("Successo\n");

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        fprintf(stderr, "Il numero degli argomenti non ?? valido\n");
        return -1;
    }

    ignoreSigpipe();

    //Inizializzazione liste
    EQ_NULL_EXIT(requestLst = list_create(requestLst, compareRequest), "list_create")
    EQ_NULL_EXIT(dirLst = list_create(dirLst, str_compare), "list_create")
    EQ_NULL_EXIT(DdirLst = list_create(DdirLst, str_compare), "list_create")

    char* socketName = NULL;
    long flagT = 0;
    int flagR = 0;
    int flagr = 0;
    int flagd = 0;
    int flagw = 0;
    int flagW = 0;

    //Parsing dei parametri da riga di comando
    int opt;
    while((opt = getopt(argc, argv, ":hf:w:W:D:r:R:d:t:l:u:c:p")) != -1) {
        switch (opt) {
        case 'h': {
            arg_h(argv[0]);
            SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
            SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
            SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
            return 0;
        }
        case 'f': {
            if (socketName == NULL)
                socketName = optarg;
            else {
                fprintf(stderr, "Il flag -f pu?? essere specificato una sola volta\n");
                SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case 'w': arg_w(optarg); flagw = 1; break;
        case 'W': arg_W(&optarg); flagW = 1; break;
        case 'D': {
            if (flagw || flagW) { //?? stato specificato il flag '-w' o '-W'
                arg_D(optarg);
            } else {
                fprintf(stderr, "Il flag -D va utilizzato dopo aver specificato -w o -W\n");
                SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
            flagw = 0;
            flagW = 0;
            break;
        }
        case 'r': {
            arg_r(&optarg);
            flagr = 1;
            break;
        }
        case 'R': {
            if (optarg[0] == '-') { //Non ?? stato specificato l'argomento per l'opzione -R
                optind -= 1;
                flagR = arg_R("n=0");
            } else {
                flagR = arg_R(optarg);
            }
            
            if (flagR == -1) {
                fprintf(stderr, "Error in arg_R\n");
                SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case 'd': {
            if (flagR || flagr) { //?? stato specificato il flag '-r' o '-R'
                arg_d(optarg);
            } else {
                fprintf(stderr, "Il flag -d va utilizzato dopo aver specificato -r o -R\n");
                SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
            flagR = 0;
            flagr = 0;
            break;
        }
        case 't': {
            if (isNumber(optarg, &flagT) != 0) {
                perror("isNumber");
                SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case 'l': arg_l(&optarg); break;
        case 'u': arg_u(&optarg); break;
        case 'c': arg_c(&optarg); break;
        case 'p': {
            if (!flagP)
                flagP = 1;
            else {
                fprintf(stderr, "Il flag -p pu?? essere specificato una sola volta\n");
                SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case ':': {
            if (optopt == 'R') { //L'opzione '-R' non ha un parametro
                if ((flagR = arg_R("n=0")) == -1) {
                    fprintf(stderr, "Error in arg_R\n");
                    SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                    SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                    SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                    exit(EXIT_FAILURE);
                }
                break;
            } else {
                printf("L'opzione '-%c' richiede un argomento\n", optopt);
                SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
                SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
        }
        case '?': {
            printf("L'opzione '-%c' non ?? gestita\n", optopt);
            SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
            SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
            SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
            exit(EXIT_FAILURE);
        }
        default: break;
        }
    }

    if (socketName == NULL) {
        fprintf(stderr, "Socket non specificato\n");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
        return -1;
    }

    if (flagd && !(flagR || flagr)) {
        fprintf(stderr, "Flag -d incompatibile senza -R o -r\n");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
        return -1;
    }


    //Il client tenter?? di connettersi al server al massimo per 10 secondi
    struct timespec maxTime;
    if (clock_gettime(CLOCK_REALTIME, &maxTime) == -1) {
        perror("clock_gettime");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    maxTime.tv_sec += 10;

    if (flagP) printf("Connessione al server in corso...\n");
    //Connessione al server
    if (openConnection(socketName, 200, maxTime) == -1) {
        perror("openConnection");
        SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    if (flagP) printf("Connesso\n");

    //Esecuzione delle richieste
    while(requestLst->head != NULL) {
        //Preleva la nuova richiesta
        request_t *req = list_pop(requestLst);
        
        if (req == NULL && errno == EINVAL) {
            perror("list_pop");
            exit(EXIT_FAILURE);
        }

        switch (req->flag) {
            case 'w': {
                //Preleva la directory in cui salvare gli eventuali file
                char* dir = (char*) list_pop(DdirLst);

                if (req_w(req->arg[0], req->option, dir) == -1) {perror("flag -w"); free(dir); return -1;}
                free(dir);
                break;
            }
            case 'W': {
                //Preleva la directory in cui salvare gli eventuali file
                char* dir = (char*) list_pop(DdirLst);
                if (dir != NULL) {
                    //Verifica che dir sia una directory
                    struct stat info;
                    if (stat(dir, &info) == -1) return -1;
                    if (!S_ISDIR(info.st_mode)) {
                        errno = ENOTDIR;
                        return -1;
                    }
                }

                for(int i = 0; i < req->dim; i++) {
                    if (req_W(req->arg[i], dir) == -1) {perror("flag -W"); free(dir); return -1;}
                }
                free(dir);
                break;
            }
            case 'r': {
                //Preleva la directory in cui salvare gli eventuali file
                char* dir = (char*) list_pop(dirLst);
                if (dir != NULL) {
                //Verifica che dir sia una directory
                    struct stat info;
                    if (stat(dir, &info) == -1) return -1;
                    if (!S_ISDIR(info.st_mode)) {
                        errno = ENOTDIR;
                        return -1;
                    }
                }

                for(int i = 0; i < req->dim; i++) {
                    if (flagP) printf("Apertura file %s\n", req->arg[i]);
                    if (openFile(req->arg[i], 0) == -1) {perror("openFile"); return -1;}
                    if (flagP) printf("File %s aperto correttamente\n", req->arg[i]);
                    
                    void* buffer;
                    size_t size;

                    if (flagP) printf("Lettura del file %s\n", req->arg[i]);
                    if (readFile(req->arg[i], &buffer, &size) == -1) {perror("writeFile"); return -1;}
                    if (flagP) printf("File %s letto correttamente. Letti %ld byte\n", req->arg[i], size);
                    
                    if(dir != NULL) {
                        //Creazione del file nella directory specificata
                        int fd;
                        if (flagP) printf("Creazione del file nella directory %s\n", dir);
                        if ((fd = createLocalFile(dir, req->arg[i])) == -1) {perror("createLocalFile"); return -1;}
                        if (flagP) printf("File creato correttamente\n");

                        //Scrittura nel file
                        if (flagP) printf("Scrittura nel file\n");
                        if (writen(fd, buffer, size) == -1) {perror("writen"); return -1;}
                        if (flagP) printf("Scrittura completata con successo. Scritti %ld byte\n", size);
                        
                        //Chiusura del file
                        close(fd);
                    }
                    free(buffer);

                    if (flagP) printf("Chiusura file %s\n", req->arg[i]);
                    if (closeFile(req->arg[i]) == -1) {perror("closeFile"); return -1;}
                    if (flagP) printf("File %s chiuso correttamente\n", req->arg[i]);
                }
                free(dir);
                break;
            }
            case 'R': {
                int r;
                //Preleva la directory in cui salvare gli eventuali file
                char* dir = (char*) list_pop(dirLst);

                if (flagP && req->option != 0) printf("Lettura di %d file dal server\n", req->option);
                if (flagP && req->option == 0) printf("Lettura di tutti i file dal server\n");
                if ((r = readNFiles(req->option, dir)) == -1) {perror("readNFile"); return -1;}
                if (flagP) printf("Lettura %d file dal server correttamente\n", r);

                free(dir);
                break;
            }
            case 'l': {
                //arg[0] lo locko con openFile solo per provare anche questa funzione
                if (flagP) printf("Lock del file %s\n", req->arg[0]);
                if (openFile(req->arg[0], O_LOCK) == -1) {perror("lockFile"); return -1;}
                if (flagP) printf("Successo\n");

                for(int i = 1; i < req->dim; i++) {
                    if (flagP) printf("Apertura file %s\n", req->arg[i]);
                    if (openFile(req->arg[i], 0) == -1) {perror("openFile"); return -1;}
                    if (flagP) printf("File %s aperto correttamente\n", req->arg[i]);

                    if (flagP) printf("Lock del file %s\n", req->arg[i]);
                    if (lockFile(req->arg[i]) == -1) {perror("lockFile"); return -1;}
                    if (flagP) printf("Successo\n");
                }
                break;
            }
            case 'u': {
                for(int i = 0; i < req->dim; i++) {
                    if (flagP) printf("Unlock del file %s\n", req->arg[i]);
                    if (unlockFile(req->arg[i]) == -1) {perror("unlockFile"); return -1;}
                    if (flagP) printf("Successo\n");

                    if (flagP) printf("Chiusura file %s\n", req->arg[i]);
                    if (closeFile(req->arg[i]) == -1) {perror("closeFile"); return -1;}
                    if (flagP) printf("File %s chiuso correttamente\n", req->arg[i]);
                }
                break;
            }
            case 'c': {
                for(int i = 0; i < req->dim; i++) {
                    if (flagP) printf("Apertura file %s\n", req->arg[i]);
                    if (openFile(req->arg[i], 0) == -1) {perror("openFile"); return -1;}
                    if (flagP) printf("File %s aperto correttamente\n", req->arg[i]);

                    if (flagP) printf("Rimozione del file %s\n", req->arg[i]);
                    if (removeFile(req->arg[i]) == -1) {perror("removeFile"); return -1;}
                    if (flagP) printf("Successo\n");
                }
                break;
            }

            default: {
                fprintf(stderr, "Errore nel recuperare le richieste\n");
                exit(EXIT_FAILURE);
            }
        }

        //Attesa tra una richiesta e l'altra
        if(flagT != 0) {
            struct timespec nextReq;
            nextReq.tv_sec = flagT / 1000;
            nextReq.tv_nsec = (flagT % 1000) * 1000000;
            int res;
            do {
                res = nanosleep(&nextReq, &nextReq);
            } while(res && errno == EINTR);
        }

        freeRequest(req);
    }

    //Chiude la connessione
    if (flagP) printf("Chiusura connessione...\n");
    if (closeConnection(socketName) == -1) {perror("closeConnection"); exit(EXIT_FAILURE);}
    if (flagP) printf("Chiusura completata con successo\n");

    //Libera la memoria
    SYSCALL_ONE_EXIT(list_destroy(requestLst, freeRequest), "list_destroy");
    SYSCALL_ONE_EXIT(list_destroy(dirLst, freeRequest), "list_destroy");
    SYSCALL_ONE_EXIT(list_destroy(DdirLst, freeRequest), "list_destroy");
    return 0;
}