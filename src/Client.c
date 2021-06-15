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

#include "../includes/util.h"
#include "../includes/comunicationProtocol.h"
#include "../includes/list.h"
#include "../includes/comunication.h"

#define STRLEN 512

typedef struct _request {
    char flag;
    int option;
    char** arg;
    int dim;
} request_t;

list_t requestLst;
list_t dirLst;
list_t DdirLst;

int flagP = 0;

int compareRequest(void* a, void* b) {
    request_t *reqA = (request_t*) a;
    request_t *reqB = (request_t*) b;

    if (reqA->flag != reqB->flag || reqA->option != reqB->option || reqA->dim != reqB->dim) return 0;
    for(int i = 0; i < reqA->dim; i++)
        if (strncmp(reqA->arg[i], reqB->arg[i], STRLEN) != 0) return 0;
    return 1;
}

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

char* getabspath(char* path) {
    int len = strnlen(path, STRLEN) + 1;
        
    int i;
    for(i = len - 1; i >= 0; i--) {
        if (path[i] == '/') break;
    }

    if (i == -1) {
        char* cwd = calloc(STRLEN, sizeof(char));
        if (cwd == NULL) return NULL;

        int len_cwd = STRLEN;
        if((cwd = getcwd(cwd, len_cwd)) == NULL) {
            if (errno != ERANGE) {
                perror("getcwd");
                free(cwd);
                return NULL;
            }
            do {
                len_cwd *= 2;
                char* tmp = realloc(cwd, len_cwd);
                if (tmp == NULL) {perror("realloc in req_W"); return NULL;}
                cwd = tmp;
            } while((cwd = getcwd(cwd, len_cwd)) == NULL);
        }

        if (len_cwd <= strnlen(cwd, len_cwd) + len) {
            cwd += (len - i);
            char* tmp = realloc(cwd, len_cwd * sizeof(char));
            if (tmp == NULL) {
                free(cwd);
                return NULL;
            }
            cwd = tmp;
        }

        strncat(cwd, "/", 2);
        strncat(cwd, path, len);

        return cwd;
    }
    
    char* dirpath = calloc(i, sizeof(char));
    if (dirpath == NULL) return NULL;

    strncpy(dirpath, path, i);

    char* cwd = calloc(STRLEN, sizeof(char));
    if (cwd == NULL) {free(dirpath); return NULL;}

    int len_cwd = STRLEN;
    if((cwd = getcwd(cwd, len_cwd)) == NULL) {
        if (errno != ERANGE) {
            perror("getcwd");
            free(cwd);
            free(dirpath);
            return NULL;
        }
        do {
            len_cwd *= 2;
            char* tmp = realloc(cwd, len_cwd);
            if (tmp == NULL) {perror("realloc in req_W"); return NULL;}
            cwd = tmp;
        } while((cwd = getcwd(cwd, len_cwd)) == NULL);
    }

    if (chdir(dirpath) == -1) {
        fprintf(stderr, "Percorso del file invalido\n");
        free(cwd);
        free(dirpath);
        return NULL;
    }


    char* cwd_file = calloc(STRLEN, sizeof(char));
    if (cwd_file == NULL) {free(path); return NULL;}

    int len_cwd_file = STRLEN;
    if((cwd_file = getcwd(cwd_file, len_cwd_file)) == NULL) {
        if (errno != ERANGE) {
            perror("getcwd");
            free(cwd_file);
            free(cwd);
            free(dirpath);
            return NULL;
        }
        do {
            len_cwd_file *= 2;
            char* tmp = realloc(cwd, len_cwd_file);
            if (tmp == NULL) return NULL;
            cwd_file = tmp;
        } while((cwd_file = getcwd(cwd_file, len_cwd_file)) == NULL);
    }

    if (len_cwd_file <= strnlen(cwd_file, len_cwd_file) + (len - i)) {
        len_cwd_file += (len - i);
        char* tmp = realloc(cwd_file, len_cwd_file * sizeof(char));
        if (tmp == NULL) {
            free(cwd);
            free(cwd_file);
            free(dirpath);
            return NULL;
        }
        cwd_file = tmp;
    }

    strncat(cwd_file, path + i, len_cwd_file);

    if (chdir(cwd) == -1) {
        free(cwd);
        free(cwd_file);
        free(dirpath);
        return NULL;
    }

    free(cwd);
    free(dirpath);

    return cwd_file;
}

char** tokString (char** str, int* initSize) {
    *initSize = countComma(*str) + 1;

    char** arg = malloc(sizeof(char*) * (*initSize));

    if (arg == NULL) {
        perror("malloc");
        return NULL;
    }

    // for(int i = 0; i < *initSize; i++) {
    //     arg[i] = calloc(STRLEN, sizeof(char));

    //     if (arg[i] == NULL) {

    //         for(int j = 0; j < i; j++)
    //             free(arg[j]);
    //         free(arg);

    //         perror("calloc");
    //         return NULL;
    //     }

    // }

    char *state = NULL;
    char *token = strtok_r(*str, ",", &state);

    int i = 0;
    while(token) {
        char* file = getabspath(token);
        if (file == NULL) {perror("getabspath"); return NULL;}
        int filelen = strnlen(file, STRLEN) + 1;
        arg[i] = calloc(filelen, sizeof(char));
        if (arg[i] == NULL) {
            fprintf(stderr, "Errore critico in memoria\n");
            return NULL;
        }
        strncpy(arg[i], file, filelen);
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
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
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
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    newR->arg[0] = calloc(STRLEN, sizeof(char));
    if(newR->arg[0] == NULL) {
        perror("malloc in arg[0] in arg_w");
        free(newR->arg);
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    strncpy(newR->arg[0], arg, strlen(arg));

    if (lenDir == -1) {
        newR->option = 0;
    } else {
        long tmp;

        if (isNumber(arg + lenDir + 2, &tmp) != 0) {
            fprintf(stderr, "Errore in isNumber in arg_w\n");
            freeRequest(newR);
            SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
            exit(EXIT_FAILURE);
        }
        newR->option = tmp;
    }

    if (list_append(&requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
    }
}

void arg_W(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_W");
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'W';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    if (list_append(&requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
    }
}

void arg_r(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_r");
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'r';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    if (list_append(&requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
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
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy")
        exit(EXIT_FAILURE);
    }
    newR->flag = 'R';
    newR->dim = 0;
    newR->arg = NULL;

    long tmp;
    if (isNumber(arg+2, &tmp) != 0) {
        fprintf(stderr, "isNumber in case 'R'\n");
        free(newR);
        return -1;
    }
    newR->option = tmp;

    if (list_append(&requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        return -1;
    }

    return 1;
}

void arg_c(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_c");
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    newR->flag = 'c';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    
    if (list_append(&requestLst, newR) == -1) {
        freeRequest(newR);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
}

void arg_d(char* arg) {    
    int len = strnlen(arg, STRLEN) + 1;
    char* newD = calloc(len, sizeof(char));
    if(newD == NULL) {
        perror("calloc in arg_d");
        free(newD);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(&dirLst, free), "list_destroy");
        exit(EXIT_FAILURE);
    }

    strncpy(newD, arg, len);

    if (list_append(&dirLst, newD) == -1) {
        free(newD);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(&dirLst, free), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(&DdirLst, free), "list_destroy");
        exit(EXIT_FAILURE);
    }
}

void arg_D(char* arg) {    
    int len = strnlen(arg, STRLEN) + 1;
    char* newD = calloc(len, sizeof(char));
    if(newD == NULL) {
        perror("calloc in arg_d");
        free(newD);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(&dirLst, free), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(&DdirLst, free), "list_destroy");
        exit(EXIT_FAILURE);
    }

    strncpy(newD, arg, len);

    if (list_append(&DdirLst, newD) == -1) {
        free(newD);
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(&dirLst, free), "list_destroy");
        SYSCALL_ONE_EXIT(list_destroy(&DdirLst, free), "list_destroy");
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
        perror("calloc in req_W");
        return -1;
    }

    //Nell'evenutlità che non ci sia abbastanza memoria allocata, la rialloca
    int len = STRLEN;
    if((cwd = getcwd(cwd, len)) == NULL) {
        if (errno != ERANGE) {perror("getcwd"); return -1;}
        do {
            len *= 2;
            char* tmp = realloc(cwd, len);
            if (tmp == NULL) {perror("realloc in req_W"); return -1;}
            cwd = tmp;
        } while((cwd = getcwd(cwd, len)) == NULL);
    }

    //Sposta la cwd in quella della cartella in analisi
    if (chdir(dir) == -1) {perror("chdir"); return -1;}

    while ((errno = 0, currentFile = readdir(d)) != NULL) {

        if (strncmp(".", currentFile->d_name, 2) != 0 && strncmp("..", currentFile->d_name, 3) != 0 && *n != 0) {

            char* filepath = calloc(STRLEN, sizeof(char));
            if (filepath == NULL) {perror("calloc in inspectDir"); return -1;}

            int len = STRLEN;
            if((filepath = getcwd(filepath, len)) == NULL) {
                if (errno != ERANGE) {perror("getcwd"); return -1;}
                do {
                    len *= 2;
                    char* tmp = realloc(filepath, len);
                    if (tmp == NULL) {perror("realloc in req_W"); return -1;}
                    filepath = tmp;
                } while((filepath = getcwd(filepath, len)) == NULL);
            }

            if (strnlen(filepath, len) + strnlen(currentFile->d_name, STRLEN) > len) {
                len += strnlen(currentFile->d_name, STRLEN);
                char* tmp = realloc(filepath, len);
                if (tmp == NULL) {perror("realloc in req_W"); return -1;}
                filepath = tmp;
            }

            strncat(filepath, "/", len);
            strncat(filepath, currentFile->d_name, len);

            struct stat statFile;
            if (stat(filepath, &statFile) == -1) {
                perror("During stat");
                return -1;
            }

            if (S_ISDIR(statFile.st_mode)) {
                inspectDir(filepath, n, saveDir);
            } else {
                // int fd;
                //Apre il file e lo crea sul server
                // if (flagP) printf("Apertura del file %s in lettura... ", filepath);
                // if ((fd = open(filepath, O_RDONLY)) == -1) {perror("open"); return -1;}
                // if (flagP) printf("Successo\n");

                if (flagP) printf("Apertura del file nel server... ");
                if (openFile(filepath, O_CREATE) == -1) {perror("openFile"); return -1;}
                if (flagP) printf("Successo\n");

                if (flagP) printf("Scrittura del file %s nel server\n", filepath);
                if (writeFile(filepath, saveDir) == -1) {perror("writeFile"); return -1;}
                if (flagP) printf("Scrittura completata con successo\n");
                
                if (flagP) printf("Chiusura del file %s... ", filepath);
                if (closeFile(filepath) == -1) {perror("closeFile"); return -1;}
                if (flagP) printf("Successo\n");
                // char* tmp = calloc(STRLEN, sizeof(char));
                // if (tmp == NULL) {perror("calloc"); return -1;}   
                // int res;
                // //Scrive il file sul server STRLEN byte alla volta
                // if (flagP) printf("Scrittura del file %s nel server\n", filepath);
                // do {
                //     int len;
                //     if ((res = readn(fd, tmp, STRLEN)) == -1) {perror("readn"); return -1;}
                //     if (res == 0) len = strnlen(tmp, STRLEN) + 1;
                //     else len = res;
                //     if (appendToFile(filepath, tmp, len, saveDir) == -1) {perror("appendToFile"); return -1;} //Qui modificare il NULL se implemento il -D
                //     memset(tmp, 0, STRLEN);
                // } while(res == 0);
                // if (flagP) printf("Scrittura del file %s avvenuta con successo\n", filepath);
                // free(tmp);

                (*n)--;
            }
            free(filepath);
        }
    }
    if (errno != 0) {
        perror("Error reading directory");
        return -1;
    }

    if (chdir(cwd) == -1) {perror("chdir"); return -1;}
    free(cwd);

    if (closedir(d) == -1) {perror("Closing directory"); return -1;}

    return 0;
}

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

int createLocalFile(char* dirname, char* path) {
    int len = strnlen(path, STRLEN) + 1;
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
        if (extension == NULL) return -1;

        strncpy(extension, path + fullstop, len - fullstop);
    }

    //Salvataggio e cambio della CWD per poter salvare i file
    char* cwd = calloc(STRLEN, sizeof(char));
    if (cwd == NULL) {
        fprintf(stderr, "Errore critico in memoria\n");
        if (fullstop != -1) free(extension);
        return -1;
    }

    //Nell'evenutlità che non ci sia abbastanza memoria allocata, la rialloca
    int len_cwd = STRLEN;
    if((cwd = getcwd(cwd, len_cwd)) == NULL) {
        if (errno != ERANGE) {
            perror("getcwd"); 
            if (fullstop != -1) free(extension);
            free(cwd);
            return -1;
        }
        do {
            len_cwd *= 2;
            char* tmp = realloc(cwd, len_cwd);
            if (tmp == NULL) {perror("realloc in req_W"); return -1;}
            cwd = tmp;
        } while((cwd = getcwd(cwd, len_cwd)) == NULL);
    }

    //Cambio della CWD
    if (chdir(dirname) == -1) {
        if (fullstop != -1) free(extension);
        free(cwd);
        return -1;
    }

    //Creazione del file. Se esiste un file con lo stesso nome verrà modificato il nome del file da creare
    int createdFile, oldCifre;
    int try = 1;
    while((createdFile = open(path + startName, O_WRONLY | O_CREAT | O_EXCL, 0666)) == -1) {
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

    return createdFile;
}

int req_W(const char* path, char* saveDir) {
    if (path == NULL) return -1;

    if (flagP) printf("Apertura del file nel server\n");
    if (openFile(path, O_CREATE) == -1) {perror("openFile"); return -1;}
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
        fprintf(stderr, "Il numero degli argomenti non è valido\n");
        return -1;
    }

    ignoreSigpipe();    

    SYSCALL_ONE_EXIT(list_create(&requestLst, compareRequest), "list_create")
    SYSCALL_ONE_EXIT(list_create(&dirLst, str_compare), "list_create")
    SYSCALL_ONE_EXIT(list_create(&DdirLst, str_compare), "list_create")

    char* socketName = NULL;
    long flagT = 0;
    int flagR = 0;
    int flagr = 0;
    int flagd = 0;
    int flagw = 0;
    int flagW = 0;

    int opt;
    while((opt = getopt(argc, argv, ":hf:w:W:D:r:R:d:t:l:u:c:p")) != -1) {
        switch (opt) {
        case 'h': {
            arg_h(argv[0]);
            SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
            return 0;
        }
        case 'f': {
            if (socketName == NULL)
                socketName = optarg;
            else {
                fprintf(stderr, "Il flag -f può essere specificato una sola volta\n");
                SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
                return -1;
            }
            break;
        }
        case 'w': arg_w(optarg); flagw = 1; break;
        case 'W': arg_W(&optarg); flagW = 1; break;
        case 'D': {
            if (flagw || flagW) {
                arg_D(optarg);
            } else {
                fprintf(stderr, "Il flag -D va utilizzato dopo aver specificato -w o -W\n");
                SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
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
            if ((flagR = arg_R(optarg)) == -1) {
                fprintf(stderr, "Error in arg_R\n");
                SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
                exit(EXIT_FAILURE);
            }
            break;
        }
        case 'd': {
            if (flagR || flagr) {
                arg_d(optarg);
            } else {
                fprintf(stderr, "Il flag -d va utilizzato dopo aver specificato -r o -R\n");
                SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
            }
            flagR = 0;
            flagr = 0;
            break;
        }
        case 't': {
            if (isNumber(optarg, &flagT) != 0) {
                perror("isNumber");
                SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
                return -1;
            }
            break;
        }
        case 'l': printf("L'operazione -l non è gestita\n"); break;
        case 'u': printf("L'operazione -u non è gestita\n"); break;
        case 'c': arg_c(&optarg); break;
        case 'p': {
            if (!flagP)
                flagP = 1;
            else {
                fprintf(stderr, "Il flag -p può essere specificato una sola volta\n");
                SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
                return -1;
            }
            break;
        }
        case ':': {
            if (optopt == 'R') {
                flagR = 0;
                break;
            } else {
                printf("L'opzione '-%c' richiede un argomento\n", optopt);
                SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
                return 0;
            }
        }
        case '?': {
            printf("L'opzione '-%c' non è gestita\n", optopt);
            SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
            return 0;
        }
        default: break;
        }
    }

    if (socketName == NULL) {
        fprintf(stderr, "Socket non specificato\n");
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        return -1;
    }

    if (flagd && !(flagR || flagr)) {
        fprintf(stderr, "Flag -d incompatibile senza -R o -r\n");
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        return -1;
    }


    //Provvisorio
    struct timespec maxTime;
    if (clock_gettime(CLOCK_REALTIME, &maxTime) == -1) {
        perror("clock_gettime");
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }
    maxTime.tv_sec += 10;

    if (flagP) printf("Connessione al server in corso...\n");
    //Connessione al server
    if (openConnection(socketName, 200, maxTime) == -1) {
        perror("openConnection");
        SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
        exit(EXIT_FAILURE);
    }

    while(requestLst.head != NULL) {
        request_t *req = list_pop(&requestLst);
        
        if (req == NULL && errno == EINVAL) {
            perror("list_pop");
            exit(EXIT_FAILURE);
        }

        switch (req->flag) {
            case 'w': {
                char* dir = (char*) list_pop(&DdirLst);
                if (dir != NULL) {
                    //Verifica che dir sia una directory
                    struct stat info;
                    if (stat(dir, &info) == -1) return -1; //Forse conviene spostare questi controlli nella fase di parsing
                    if (!S_ISDIR(info.st_mode)) {
                        errno = ENOTDIR;
                        return -1;
                    }
                }
                if (req_w(req->arg[0], req->option, dir) == -1) {perror("flag -w"); free(dir); return -1;}
                free(dir);
                break;
            }
            case 'W': {
                char* dir = (char*) list_pop(&DdirLst);
                if (dir != NULL) {
                    //Verifica che dir sia una directory
                    struct stat info;
                    if (stat(dir, &info) == -1) return -1; //Forse conviene spostare questi controlli nella fase di parsing
                    if (!S_ISDIR(info.st_mode)) {
                        errno = ENOTDIR;
                        return -1;
                    }
                }
                for(int i = 0; i < req->dim; i++) {
                    if (req_W(req->arg[i], dir) == -1) {perror("req_w"); free(dir); return -1;}
                }
                free(dir);
                break;
            }
            case 'r': {
                char* dir = (char*) list_pop(&dirLst);
                if (dir != NULL) {
                //Verifica che dir sia una directory
                    struct stat info;
                    if (stat(dir, &info) == -1) return -1; //Forse conviene spostare questi controlli nella fase di parsing
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
                        if (flagP) printf("Creazione del file nella directory %s...\n", dir);
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
                char* dir = (char*) list_pop(&dirLst);
                if (flagP && req->option != 0) printf("Lettura di %d file dal server\n", flagR);
                if (flagP && req->option == 0) printf("Lettura di tutti i file dal server\n");
                if (readNFiles(req->option, dir) == -1) {perror("readNFile"); return -1;}
                if (flagP) printf("Lettura dei file dal server completata correttamente\n");
                free(dir);
                break;
            }
            case 'c': break;
        }

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

    if (closeConnection(socketName) == -1) {perror("closeConnection"); exit(EXIT_FAILURE);}

    SYSCALL_ONE_EXIT(list_destroy(&requestLst, freeRequest), "list_destroy");
    return 0;
}