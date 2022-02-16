/* Sistemi Operativi - prova di laboratorio
 *  compito del 16/09/2016 - soluzione       */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_NUM_SAMPLES 30
#define PAUSE_SECS 1
#define MAX_LEN_BUFFER 1024
#define PROC_PATHNAME "/proc/stat"
#define DISPLAY_COLUMNS 60

/* 3 semafori */
enum SEM_TYPE { S_EMPTY = 0, S_RAW, S_DELTA };

typedef struct {
    long int user;
    long int system;
    long int idle;
} raw_data;

typedef struct {
    float user;
    float system;
} delta_data;

typedef struct {
    union {
        raw_data raw;
        delta_data delta;
    } content;
    char raw_eof;
    char delta_eof;
} shm_data;

/**
 * l'utilizzo della union nella struttura shm_data permette di utilizzare un
 * segmento più piccolo; è accettabile anche utilizzare campi distinti che
 * coprano tutti i casi di utilizzo previsti, ovvero:
 * typedef struct {
 *     long int raw_user;
 *     long int raw_system;
 *     long int raw_idle;
 *     float delta_user;
 *     float delta_system;
 *     char raw_eof;
 *     char delta_eof;
 * }  shm_data;
 */

int WAIT(int sem_des, short unsigned int sem_num) {
    struct sembuf ops[1] = {{sem_num, -1, 0}};

    return semop(sem_des, ops, 1);
}
int SIGNAL(int sem_des, short unsigned int sem_num) {
    struct sembuf ops[1] = {{sem_num, +1, 0}};

    return semop(sem_des, ops, 1);
}

/* il figlio sampler che si occupa di campionare dal file-system /proc/ */
void sampler(shm_data *data, int sem, int num_samples) {
    FILE *proc_fs;
    char buffer[MAX_LEN_BUFFER];
    long int ignore_it;

    for (int i = 0; i < num_samples; i++) {
        if ((proc_fs = fopen(PROC_PATHNAME, "r")) == NULL) {
            perror(PROC_PATHNAME);
            exit(1);
        }
        if (fgets(buffer, MAX_LEN_BUFFER, proc_fs) == NULL) {
            perror(PROC_PATHNAME);
            exit(1);
        }
        fclose(proc_fs);

        /* aspetto che si liberi l'area condivisa */
        WAIT(sem, S_EMPTY);

        sscanf(buffer, "cpu  %ld %ld %ld %ld %ld %ld %ld",
               &(data->content.raw.user), &ignore_it,
               &(data->content.raw.system), &(data->content.raw.idle),
               &ignore_it, &ignore_it, &ignore_it);

        /* se si tratta dell'ultimo sample campionato, lo segnalo */
        if (i == num_samples - 1)
            data->raw_eof = 1;

        /* segnalo a chi attende dati raw (Analyzer), che questi
         * sono disponibili */
        SIGNAL(sem, S_RAW);

        sleep(PAUSE_SECS);
    }

    exit(0);
}

/* il figlio analyzer che si occupa di tradurre i sample sugli utilizzi delle
 * CPU in percentuali di variazione */
void analyzer(shm_data *data, int sem) {
    long int last_user = -1, last_system = -1, last_idle = -1;
    long int total;
    float new_user, new_system;

    while (!data->delta_eof) {
        /* attendo che ci sia dati raw nell'area condivisa */
        WAIT(sem, S_RAW);

        /* se si tratta dell'ultimo campione raw, allora
         * il deta che stiamo per calcolare sarà anche l'ultimo */
        if (data->raw_eof)
            data->delta_eof = 1;

        /* se questo è il primo campione ricevuto, non posso ancora
         * calcolare i valori delta; in questo caso Analyzer si deve
         * occupare di segnare come libera l'area condivisa (in genere
         * viene fatto da Plotter) */
        if (last_user < 0) {
            last_user = data->content.raw.user;
            last_system = data->content.raw.system;
            last_idle = data->content.raw.idle;
            SIGNAL(sem, S_EMPTY);
        } else {
            total = (data->content.raw.user - last_user) +
                    (data->content.raw.system - last_system) +
                    (data->content.raw.idle - last_idle);

            /* dobbiamo usare le variabili locali new_user e new_system perchè
             * altrimenti rischieremmo di sovrascivere i dati raw.user e
             * raw.system nella union durante il calcolo */
            new_user =
                (float)(data->content.raw.user - last_user) / (float)total;
            new_system =
                (float)(data->content.raw.system - last_system) / (float)total;

            last_user = data->content.raw.user;
            last_system = data->content.raw.system;
            last_idle = data->content.raw.idle;

            data->content.delta.user = new_user;
            data->content.delta.system = new_system;

            /* segnalo a chi attende dati delta (Plotter), che questi
             * sono disponibili */
            SIGNAL(sem, S_DELTA);
        }
        /* nota importante: formalmente sto estraendo dati raw e sto
         * inserendo dati delta, quindi la sequenza sui semafori dovrebbe
         * essere:
         * WAIT(sem, S_RAW); SIGNAL(sem, S_EMPTY); ...
         * WAIT(sem, S_EMPTY); SIGNAL(sem, S_DELTA);
         * in questo modo però rischieremmo uno stallo: lo vedete? */
    }

    exit(0);
}

void print_chars(unsigned int n, char c) {
    for (unsigned int i = 0; i < n; i++)
        putc(c, stdout);
}

/* il figlio plotter che si occupa di visualizzare i dati di variazione */
void plotter(shm_data *data, int sem) {
    unsigned int user_chars, system_chars, idle_chars;

    while (!data->delta_eof) {
        /* attendo che ci sia dati delta nell'area condivisa */
        WAIT(sem, S_DELTA);

        user_chars = data->content.delta.user * DISPLAY_COLUMNS;
        system_chars = data->content.delta.system * DISPLAY_COLUMNS;
        idle_chars = DISPLAY_COLUMNS - user_chars - system_chars;

        print_chars(system_chars, '#');
        print_chars(user_chars, '*');
        print_chars(idle_chars, '_');
        printf(" s:%5.2f%% u:%5.2f%%\n", data->content.delta.system * 100,
               data->content.delta.user * 100);

        /* segnalo che adesso l'area condivisa è libera */
        SIGNAL(sem, S_EMPTY);
    }

    exit(0);
}

int main(int argc, char **argv) {
    shm_data *data;
    int shm_des, sem_des;
    int num_samples;

    if ((argc == 1) || ((num_samples = atoi(argv[1])) < 2))
        num_samples = DEFAULT_NUM_SAMPLES;

    if ((shm_des = shmget(IPC_PRIVATE, sizeof(shm_data), IPC_CREAT | 0600)) ==
        -1) {
        perror("shmget");
        exit(1);
    }

    if ((data = (shm_data *)shmat(shm_des, NULL, 0)) == (shm_data *)-1) {
        perror("shmat");
        exit(1);
    }

    /* inizializza i valori di EOF nella struttura condivisa */
    data->raw_eof = data->delta_eof = 0;

    if ((sem_des = semget(IPC_PRIVATE, 3, IPC_CREAT | 0600)) == -1) {
        perror("semget");
        exit(1);
    }

    if (semctl(sem_des, S_EMPTY, SETVAL, 1) == -1) {
        perror("semctl SETVAL S_EMPTY");
        exit(1);
    }
    if (semctl(sem_des, S_RAW, SETVAL, 0) == -1) {
        perror("semctl SETVAL S_RAW");
        exit(1);
    }
    if (semctl(sem_des, S_DELTA, SETVAL, 0) == -1) {
        perror("semctl SETVAL S_DELTA");
        exit(1);
    }

    if (fork() == 0)
        sampler(data, sem_des, num_samples);
    if (fork() == 0)
        analyzer(data, sem_des);
    if (fork() == 0)
        plotter(data, sem_des);

    wait(NULL);
    wait(NULL);
    wait(NULL);

    shmctl(shm_des, IPC_RMID, NULL);
    semctl(sem_des, 0, IPC_RMID, 0);

    exit(0);
}
