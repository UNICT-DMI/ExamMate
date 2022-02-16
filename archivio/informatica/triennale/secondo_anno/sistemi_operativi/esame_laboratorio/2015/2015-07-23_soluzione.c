/* Sistemi Operativi - prova di laboratorio
   compito del 23/07/2015 - soluzione       */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#define ALPHABET_SIZE 26

/* la struttura di un messaggio scambiato tra i processi: */
typedef struct {
    long type;                                  // 1: messaggi ordinari, 2: errore
    unsigned long occurrences[ALPHABET_SIZE];   // il numero di occorrenze trovate per ogni lettera
} msg;

/* il generico figlio T-n che legge e analizza il file-n */
void child(int num, char *file, int queue) {
    msg message;
    struct stat statbuf;
    int fd;
    long i;
    char *map;

    /* prepara il messaggio da inviare e azzera le occorrenze */
    message.type = 1;
    for (i = 0; i < ALPHABET_SIZE; i++)
        message.occurrences[i] = 0;
    
    /* apre il file e lo mappa in memoria */
    if (stat(file, &statbuf) || !S_ISREG(statbuf.st_mode)) {
        message.type = 2;
        msgsnd(queue, &message, sizeof(message) - sizeof(long), 0);
        exit(1);
    }
    if (((fd = open(file, O_RDONLY)) == -1) || ((map = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED)) {
        message.type = 2;
        msgsnd(queue, &message, sizeof(message) - sizeof(long), 0);
        exit(2);
    }
    
    /* calcola le occorrenze delle sole lettere */
    for (i = 0; i < statbuf.st_size; i++)
        if ((map[i] >= 'a') && (map[i] <= 'z'))
            message.occurrences[map[i] - 'a']++;
        else if ((map[i] >= 'A') && (map[i] <= 'Z'))
            message.occurrences[map[i] - 'A']++;
    
    printf("processo T-%d su file '%s':\n ", num, file);
    for (i = 0; i < ALPHABET_SIZE; i++)
        printf("%c:%ld ", 'a' + i, message.occurrences[i]);
    printf("\n");
    
    /* invia il messaggio con i dati raccolti */
    if (msgsnd(queue, &message, sizeof(message) - sizeof(long), 0) == -1)
        perror("msgsnd");
    
    exit(0);
}

int main(int argc, char **argv) {
    int queue;
    int children, i, j, max, exaequo;
    msg message;
    unsigned long total_occ[ALPHABET_SIZE];

    if (argc <= 1) {
        fprintf(stderr, "uso: %s <file 1> <file 2> ...\n", argv[0]);
        exit(1);
    }

    if ((queue = msgget(IPC_PRIVATE, IPC_CREAT|0600)) == -1) {
        perror("msgget");
        exit(1);
    }

    /* crea i processi figli necessari */
    for (i = 1; i < argc; i++)
        if (fork() == 0)
            child(i, argv[i], queue);
    children = argc-1;
    
    /* azzera i totali */
    for (i = 0; i < ALPHABET_SIZE; i++)
        total_occ[i] = 0;
    
    /* attende tanti messaggi quanti sono i figli */
    for (i = 0; i < children; i++) {
        if (msgrcv(queue, &message, sizeof(message) - sizeof(long), 0, 0) == -1)
            perror("msgrcv");
        if (message.type == 2) {
            printf("errore dal figlio!\n");
            continue;
        }
        
        /* aggrega i dati del messaggio ai totali */
        for (j = 0; j < ALPHABET_SIZE; j++)
            total_occ[j] += message.occurrences[j];
    }
    
    /* visualizza i totali e controlla se c'è un massimo assoluto */
    printf("processo padre P:\n ");
    max = 0; exaequo = 0;
    for (i = 0; i < ALPHABET_SIZE; i++) {
        printf("%c:%ld ", 'a' + i, total_occ[i]);
        if (total_occ[i] == total_occ[max])
            exaequo = 1;
        else if (total_occ[i] > total_occ[max]) {
            max = i;
            exaequo = 0;
        }
    }
    printf("\n");
    if (!exaequo)
        printf(" lettera più utilizzata: '%c'\n", 'a' + max);
    
    /* rimuove la coda persistente */
    msgctl(queue, IPC_RMID, NULL);
}
