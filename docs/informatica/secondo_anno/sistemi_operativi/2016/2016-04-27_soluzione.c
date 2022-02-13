/* Sistemi Operativi - prova di laboratorio
 *  compito del 12/06/2016 - soluzione       */

#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DIM_LIST 10
#define DIM_BUFFER 1024

/* 4 semafori */
enum SEM_TYPE { S_MUTEX, S_EMPTY, S_FULL_NUM, S_FULL_RES };

/* i tipi di dati che si possono trovare nella lista */
enum DATA_TYPE { T_NUMBER = 0, T_RESIDUE = 1, T_EOF, T_EMPTY };

/* l'elemento di base della lista nella memoria condivisa */
typedef struct {
    long number;
    char type;
} shm_msg;

int WAIT(int sem_des, short unsigned int sem_num) {
    struct sembuf ops[1] = {{sem_num, -1, 0}};

    return semop(sem_des, ops, 1);
}
int SIGNAL(int sem_des, short unsigned int sem_num) {
    struct sembuf ops[1] = {{sem_num, +1, 0}};

    return semop(sem_des, ops, 1);
}

/* il codice del figlio MOD che converte numberi in residui modulo dato */
void child_mod(int sem_des, shm_msg *shared_list, long modulus) {
    char eof = 0;
    int i;
    long count = 0;

    while (1) {
        WAIT(sem_des, S_FULL_NUM);
        WAIT(sem_des, S_MUTEX);

        /* cerca un elemento di tipo T_NUMBER e lo converte */
        for (i = 0; i < DIM_LIST; i++)
            if (shared_list[i].type == T_NUMBER) {
                shared_list[i].number %= modulus;
                shared_list[i].type = T_RESIDUE;
                count++;
                break; // converte un elemento alla volta (non indispensabile)
            } else if (shared_list[i].type ==
                       T_EOF) { // se incontra T_EOF ne tiene nota
                eof = 1;
            }

        SIGNAL(sem_des, S_MUTEX);
        SIGNAL(sem_des, S_FULL_RES);

        /* se ho incontrato T_EOF e ho esaminato l'intera lista all'ultima
           iterazione, allora ho finito */
        if (eof && (i == DIM_LIST))
            break;
    }

    fprintf(stderr, "M: %lu modded elements\n", count);

    exit(0);
}

/* il codice del figlio OUT che visualizza sullo standard output i residui */
void child_out(int sem_des, shm_msg *shared_list) {
    char eof = 0;
    int i;
    long count = 0;

    while (1) {
        WAIT(sem_des, S_FULL_RES);
        WAIT(sem_des, S_MUTEX);

        /* cerca un elemento di tipo T_RESIDUE e lo stampa
           sullo standard output */
        for (i = 0; i < DIM_LIST; i++)
            if (shared_list[i].type == T_RESIDUE) {
                shared_list[i].type = T_EMPTY;
                printf("%lu\n", shared_list[i].number);
                count++;
                break; // stampa un elemento alla volta (non indispensabile)
            } else if (shared_list[i].type ==
                       T_EOF) { // se incontra T_EOF ne tiene nota
                eof = 1;
            }

        SIGNAL(sem_des, S_MUTEX);
        SIGNAL(sem_des, S_EMPTY);

        /* se ho incontrato T_EOF e ho esaminato l'intera lista all'ultima
            iterazione, allora ho finito */
        if (eof && (i == DIM_LIST))
            break;
    }

    fprintf(stderr, "O: %lu outputed elements\n", count);
    exit(0);
}

int main(int argc, char *argv[]) {
    shm_msg *shared_list;
    int shm_des, sem_des;
    long modulus;
    struct stat statbuf;
    FILE *file_input;
    long count = 0;

    if (argc != 3) {
        printf("usage: %s <input file> <modulus numbe>\n", argv[0]);
        exit(1);
    }

    if ((stat(argv[1], &statbuf) == -1) || !S_ISREG(statbuf.st_mode)) {
        perror(argv[1]);
        exit(1);
    }

    if ((modulus = atol(argv[2])) <= 0) {
        fprintf(stderr, "wrong modulus\n");
        exit(1);
    }

    if ((shm_des = shmget(IPC_PRIVATE, sizeof(shm_msg) * DIM_LIST,
                          IPC_CREAT | 0600)) == -1) {
        perror("shmget");
        exit(1);
    }

    if ((shared_list = (shm_msg *)shmat(shm_des, NULL, 0)) == (shm_msg *)-1) {
        perror("shmat");
        exit(1);
    }

    if ((sem_des = semget(IPC_PRIVATE, 4, IPC_CREAT | 0600)) == -1) {
        perror("semget");
        exit(1);
    }

    if (semctl(sem_des, S_MUTEX, SETVAL, 1) == -1) {
        perror("semctl SETVAL S_MUTEX");
        exit(1);
    }
    if (semctl(sem_des, S_EMPTY, SETVAL, DIM_LIST) == -1) {
        perror("semctl SETVAL S_EMPTY");
        exit(1);
    }
    if (semctl(sem_des, S_FULL_NUM, SETVAL, 0) == -1) {
        perror("semctl SETVAL S_FULL_NUM");
        exit(1);
    }
    if (semctl(sem_des, S_FULL_RES, SETVAL, 0) == -1) {
        perror("semctl SETVAL S_FULL_RES");
        exit(1);
    }

    if (fork() == 0) {
        child_out(sem_des, shared_list);
    } else if (fork() == 0) {
        child_mod(sem_des, shared_list, modulus);
    } else {
        char buffer[DIM_BUFFER];
        long number;

        /* rende vuoti gli elementi iniziali della lista condivisa */
        for (int i = 0; i < DIM_LIST; i++)
            shared_list[i].type = T_EMPTY;

        if ((file_input = fopen(argv[1], "r")) == NULL) {
            perror(argv[1]);
            exit(1);
        }

        while (fgets(buffer, DIM_BUFFER, file_input)) {
            number = atol(buffer);

            WAIT(sem_des, S_EMPTY);
            WAIT(sem_des, S_MUTEX);

            /* cerca uno slot vuoto... sicuro di trovarlo */
            for (int i = 0; i < DIM_LIST; i++) {
                if (shared_list[i].type == T_EMPTY) {
                    shared_list[i].type = T_NUMBER;
                    shared_list[i].number = number;
                    break;
                }
            }

            SIGNAL(sem_des, S_MUTEX);
            SIGNAL(sem_des, S_FULL_NUM);

            count++;
        }

        fprintf(stderr, "P: %lu inserted elements\n", count);

        /* inserisce un elemento di tipo T_EOF che segnalerà l'assenza di
         * ulteriori nuovi numeri */
        WAIT(sem_des, S_EMPTY);
        WAIT(sem_des, S_MUTEX);

        for (int i = 0; i < DIM_LIST; i++) {
            if (shared_list[i].type == T_EMPTY) {
                shared_list[i].type = T_EOF;
                break;
            }
        }

        SIGNAL(sem_des, S_MUTEX);

        SIGNAL(sem_des, S_FULL_NUM);
        SIGNAL(sem_des, S_FULL_RES);

        wait(NULL);
        wait(NULL);

        shmctl(shm_des, IPC_RMID, NULL);
        semctl(sem_des, 0, IPC_RMID, 0);
    }

    exit(0);
}
