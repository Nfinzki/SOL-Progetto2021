#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../includes/util.h"

#define STRLEN 256

typedef struct _request {
    char flag;
    int option;
    char** arg;
    int dim;
    struct _request *next;
} request_t;

request_t *headReq = NULL;
request_t *tailReq = NULL;

void freeRequests(request_t *r) {
    request_t *tmp;
    while(r != NULL) {
        tmp = r;
        r = r->next;
        int i = 0;
        while(tmp->arg[i] != NULL) {
            free(tmp->arg[i++]);
        }
        free(tmp->arg);
        free(tmp);
    }
}

char** tokString (char** str, int* initSize) { //Se fallisce bisogna fare la free delle alloc precedenti
    char** arg = malloc(sizeof(char*) * (*initSize)); //Contare prima il numero di virgole così non c'è bisogno di fare le realloc

    if (arg == NULL) {
        perror("malloc");
        return NULL;
    }

    for (int i = 0; i < *initSize; i++) {
        arg[i] = calloc(STRLEN, sizeof(char));

        if (arg[i] == NULL) {
            perror("calloc");
            return NULL;
        }

    }

    char *state = NULL;
    char *token = strtok_r(*str, ",", &state);

    int i = 0;
    while(token) {
        if (i == *initSize - 1) {
            *initSize *= 2;
            arg = realloc(arg, *initSize * sizeof(char*));

            if (arg == NULL) {
                perror("realloc");
                return NULL;
            }

            for(int j = i+1; j < *initSize; j++) {
                arg[j] = calloc(STRLEN, sizeof(char));

                if(arg[j] == NULL) {
                    perror("calloc");
                    return NULL;
                }
            }
        }
        strncpy(arg[i], token, strnlen(token, STRLEN));
        i++;
        token = strtok_r(NULL, ",", &state);
    }

    for(int j = i; j < *initSize; j++) {
        free(arg[j]);
        arg[j] = (char*) NULL;
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

void arg_w (char** arg) {
    request_t *newR = malloc(sizeof(request_t));
    if (newR == NULL) {
        perror("malloc in arg_w");
        freeRequests(headReq);
        exit(EXIT_FAILURE);
    }

    newR->flag = 'w';
    newR->option = 0;
    newR->dim = 1;

    //TODO Finire questo
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
    newR->dim = 4;
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
    newR->dim = 4;
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
    newR->dim = 4;
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

int main(int argc, char* argv[]) {
    if (argc == 1) {
        fprintf(stderr, "Il numero degli argomenti non è valido\n");
        return -1;
    }

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
        case 'f': socketName = optarg; break;
        case 'w': arg_w(&optarg); break;
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
        case 'd': {
            arg_d(&optarg);
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
        case 'p': flagP = 1; break;
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

    // int i = 0;
    // char* tmp = headReq->arg[i++];
    // while(tmp != NULL) {
    //     printf("%s\n", tmp);
    //     printf("i:%d\n", i);
    //     tmp = headReq->arg[i++];
    // }
    // printf("dim: %d\n", headReq->dim);

    // i = 0;
    // headReq = headReq->next;
    // tmp = headReq->arg[i++];
    // while(tmp != NULL) {
    //     printf("%s\n", tmp);
    //     printf("i:%d\n", i);
    //     tmp = headReq->arg[i++];
    // }
    // printf("dim: %d\n", headReq->dim);

    return 0;
}