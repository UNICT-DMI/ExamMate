/* Sistemi Operativi - prova di laboratorio
   compito del 15/09/2015 - soluzione       */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <ctype.h>

#define S_READER 0
#define S_WORKER 1

typedef struct {
    char c;
    char eof;
} shm_data;

int WAIT(int sem_id, int sem_num) {
    struct sembuf ops[1] = {{sem_num, -1, 0}};
    return semop(sem_id, ops, 1);
}

int SIGNAL(int sem_id, int sem_num) {
    struct sembuf ops[1] = {{sem_num, +1, 0}};
    return semop(sem_id, ops, 1);
}

void reader(int shm_id, int sem_id, FILE *input) {
    shm_data *p;
    int c;
    
    if ((p = (shm_data *) shmat(shm_id, NULL, 0)) == (shm_data *) -1) {
        perror("shmat");
        exit(1);
    }
    
    p->eof = 0;
    while ((c = fgetc(input)) != EOF) {
        WAIT(sem_id, S_READER);
        p->c = (char)c;
        SIGNAL(sem_id, S_WORKER);
    }

    WAIT(sem_id, S_READER);
    p->eof = 1;
    SIGNAL(sem_id, S_WORKER);
    
    exit(0);
}

void worker(int shm_id, int sem_id, char *filename) {
    shm_data *p;
    ulong chars, words, lines;
    char c, prev;

    if ((p = (shm_data *) shmat(shm_id, NULL, 0)) == (shm_data *) -1) {
        perror("shmat");
        exit(1);
    }

    chars = words = lines = 0;
    prev = 255;

    while (1) {
        WAIT(sem_id, S_WORKER);
        
        if (p->eof)
            break;
        else {
            c = p->c;
            chars++;
            if (isspace(c) && !isspace(prev))
                words++;
            if (c == '\n' || c == '\r')
                lines++;
            prev = c;
        }
        
        SIGNAL(sem_id, S_READER);
    }
    
    fprintf(stderr, " %lu\t%lu\t%lu\t%s\n", lines, words, chars, filename);
}

int main(int argc, char **argv) {
    FILE *in;
    int shm_id, sem_id;
    char *filename;

    if (argc >= 2) {
        if ((in = fopen(argv[1], "r")) == NULL) {
            perror(argv[1]);
            exit(1);
        }
        filename = argv[1];
    } else {
        in=stdin;
        filename = "-";
    }

    if ((shm_id = shmget(IPC_PRIVATE, sizeof(shm_data), IPC_CREAT|IPC_EXCL|0600)) == -1) {
        perror("shmget");
        exit(1);
    }

    if ((sem_id = semget(IPC_PRIVATE, 2, IPC_CREAT|IPC_EXCL|0600)) == -1) {
        perror("semget");
        exit(1);
    }

    semctl(sem_id, S_READER, SETVAL, 1);
    semctl(sem_id, S_WORKER, SETVAL, 0);
    
    if (fork() == 0)
        reader(shm_id, sem_id, in);
    else
        worker(shm_id, sem_id, filename);

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID, 0);
    fclose(in);

    exit(0);
}
