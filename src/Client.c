#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "../includes/util.h"
#include "../includes/comunicationProtocol.h"
#include "../includes/list.h"

#define STRLEN 256

typedef struct _request {
    char flag;
    int option;
    char** arg;
    int dim;
} request_t;

list_t requestLst; //Modificare tutte le vecchie liste request_t con 

void freeRequests(request_t *r) {
    request_t *tmp;
    while(r != NULL) {
        tmp = r;
        r = r->next;
        for(int i = 0; i < tmp->dim; i++) {
            free(tmp->arg[i]);
        }
        free(tmp->arg);
        free(tmp);
    }
}

int countComma (char* str) {
    int len = strnlen(str, STRLEN);
    int commas = 0;
    for(int i = 0; i < len; i++) {
        if (str[i] == ',') commas++;
    }
    return commas;
}

char** tokString (char** str, int* initSize) {
    *initSize = countComma(*str) + 1;

    char** arg = malloc(sizeof(char*) * (*initSize));

    if (arg == NULL) {
        perror("malloc");
        return NULL;
    }

    for(int i = 0; i < *initSize; i++) {
        arg[i] = calloc(STRLEN, sizeof(char));

        if (arg[i] == NULL) {

            for(int j = 0; j < i; j++)
                free(arg[j]);
            free(arg);

            perror("calloc");
            return NULL;
        }

    }

    char *state = NULL;
    char *token = strtok_r(*str, ",", &state);

    int i = 0;
    while(token) {
        strncpy(arg[i], token, strnlen(token, STRLEN));
        i++;
        token = strtok_r(NULL, ",", &state);
    }

    return arg;
}

void addTail(request_t *r) {
    if (headReq == NULL) {
        headReq = r;
        tailReq = r;
    } else {
        tailReq->next = r;
        tailReq = r;
    }
}

void arg_h (const char* name) {
    printf("usage: %s -f <filename> -w <dirname>[,n=0] -W <file1>[,file2] -D <dirname> -r <file1>[,file2] -R [n=0] -d <dirname> -t <time> -l <file1>[,file2] -u <file1>[,file2] -c <file1>[,file2] -p -h\n", name);
}

void arg_w (char* arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_w");
        freeRequests(headReq);
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
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }
    
    newR->arg[0] = calloc(STRLEN, sizeof(char));
    if(newR->arg[0] == NULL) {
        perror("malloc in arg[0] in arg_w");
        free(newR->arg);
        free(newR);
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }

    strncpy(newR->arg[0], arg, strlen(arg));

    if (lenDir == -1) {
        newR->option = 0;
    } else {
        long tmp;

        if (isNumber(arg + lenDir + 2, &tmp) != 0) {
            fprintf(stderr, "Errore in isNumber in arg_w\n");
            free(newR->arg[0]);
            free(newR->arg);
            free(newR);
            freeRequests(headReq);
            exit(EXIT_FAILURE);
        }
        newR->option = tmp;
    }

    addTail(newR);

}

void arg_W(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_W");
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }

    newR->flag = 'W';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }
    newR->next = NULL;

    addTail(newR);
}

void arg_r(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_r");
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }

    newR->flag = 'r';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }
    newR->next = NULL;

    addTail(newR);
}

void arg_c(char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_c");
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }

    newR->flag = 'c';
    newR->option = -1;
    if((newR->arg = tokString(arg, &(newR->dim))) == NULL) {
        perror("tokString");
        free(newR);
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }
    newR->next = NULL;

    addTail(newR);
}

void arg_d(char* arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_d");
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }

    newR->flag = 'd';
    newR->option = -1;
    newR->dim = 1;
    newR->arg = malloc(sizeof(char*));
    if (newR->arg == NULL) {
        perror("malloc in arg in arg_d");
        free(newR);
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }
    
    newR->arg[0] = calloc(STRLEN, sizeof(char));
    if(newR->arg[0] == NULL) {
        perror("malloc in arg[0] in arg_d");
        free(newR->arg);
        free(newR);
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }

    strncpy(newR->arg[0], arg, strnlen(arg, STRLEN));

    addTail(newR);
}

void ignoreSigpipe() {
    struct sigaction s;
    //Ignora SIGPIPE
    memset(&s, 0, sizeof(s));
    s.sa_handler = SIG_IGN;
    SYSCALL_ONE_EXIT(sigaction(SIGPIPE, &s, NULL), "sigaction");
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        fprintf(stderr, "Il numero degli argomenti non è valido\n");
        return -1;
    }

    ignoreSigpipe();    

    char* socketName = NULL;
    int flagP = 0;
    long flagT = 0;
    int flagR = 0;
    int flagr = 0;
    int flagd = 0;

    int opt;
    while((opt = getopt(argc, argv, ":hf:w:W:D:R::d:t:l:u:c:p")) != -1) {
        switch (opt) {
        case 'h': {
            arg_h(argv[0]);
            freeRequests(headReq);
            return 0;
        }
        case 'f': {
            if (socketName == NULL)
                socketName = optarg;
            else {
                fprintf(stderr, "Il flag -f può essere specificato una sola volta\n");
                freeRequests(headReq);
                return -1;
            }
            break;
        }
        case 'w': arg_w(optarg); break;
        case 'W': arg_W(&optarg); break;
        case 'D': printf("L'operazione -D non è gestita\n"); break;
        case 'r': {
            arg_r(&optarg);
            flagr = 1;
            break;
        }
        case 'R': {
            long tmp;
            if (isNumber(optarg, &tmp) != 0) {
                perror("isNumber");
                freeRequests(headReq);
                return -1;
            }
            flagR = tmp;
            break;
        }
        case 'd': { //Aggiungere i controlli che sia usato insieme a -r o -R. Forse non conviene aggiungerlo allo stack delle richieste. Devono essere possibili più -d
            arg_d(optarg);
            flagd = 1;
            break;
        }
        case 't': {
            if (isNumber(optarg, &flagT) != 0) {
                perror("isNumber");
                freeRequests(headReq);
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
                freeRequests(headReq);
                return -1;
            }
            break;
        }
        case ':': {
            printf("L'opzione '-%c' richiede un argomento\n", optopt);
            freeRequests(headReq);
            return 0;
        }
        case '?': {
            printf("L'opzione '-%c' non è gestita\n", optopt);
            freeRequests(headReq);
            return 0;
        }
        default: break;
        }
    }

    if (socketName == NULL) {
        fprintf(stderr, "Socket non specificato\n");
        freeRequests(headReq);
        return -1;
    }

    if (flagd && !(flagR || flagr)) {
        fprintf(stderr, "Flag -d incompatibile senza -R o -r\n");
        freeRequests(headReq);
        return -1;
    }

    printf("Socket %s\n", socketName);

    struct timespec maxTime; //Provvisorio
    if (clock_gettime(CLOCK_REALTIME, &maxTime) == -1) {
        perror("clock_gettime");
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }
    maxTime.tv_sec += 2;

    if (openConnection(socketName, 200, maxTime) == -1) {
        perror("openConnection");
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }
    // request_t *req = headReq;
    // while(req != NULL) {
    //     printf("Flag: -%c, option: %d\n", req->flag, req->option);

    //     for(int i = 0; i < req->dim; i++) {
    //         printf("%s\n", req->arg[i]);
    //     }
    //     printf("dim: %d\n", req->dim);
    //     req = req->next;
    // }

    freeRequests(headReq);
    return 0;
}