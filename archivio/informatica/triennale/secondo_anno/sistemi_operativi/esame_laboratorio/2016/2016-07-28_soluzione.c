/* Sistemi Operativi - prova di laboratorio
 *  compito del 28/07/2016 - soluzione       */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_NUM_SAMPLES 30
#define PAUSE_SECS 1
#define MAX_LEN_BUFFER 1024
#define PROC_PATHNAME "/proc/stat"
#define DISPLAY_COLUMNS 60

#define RAW_DATA_MSG_TYPE 1
#define DELTA_DATA_MSG_TYPE 2

typedef struct {
    long type;
    long int user;
    long int system;
    long int idle;
} raw_msg;

typedef struct {
    long type;
    float user;
    float system;
} delta_msg;

/* il figlio sampler che si occupa di campionare dal file-system /proc/ */
void sampler(int queue, int num_samples) {
    raw_msg out_message;
    FILE *proc_fs;
    char buffer[MAX_LEN_BUFFER];
    long int ignore_it;

    out_message.type = RAW_DATA_MSG_TYPE;

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

        sscanf(buffer, "cpu  %ld %ld %ld %ld %ld %ld %ld", &out_message.user,
               &ignore_it, &out_message.system, &out_message.idle, &ignore_it,
               &ignore_it, &ignore_it);

        if (msgsnd(queue, &out_message,
                   sizeof(out_message) - sizeof(out_message.type), 0) == -1)
            perror("msgsnd");

        sleep(PAUSE_SECS);
    }

    /* invia un ultimo messaggio con dati negativi per segnalare l'EOF */
    out_message.user = out_message.system = out_message.idle = -1;
    if (msgsnd(queue, &out_message,
               sizeof(out_message) - sizeof(out_message.type), 0) == -1)
        perror("msgsnd");

    exit(0);
}

/* il figlio analyzer che si occupa di tradurre i sample sugli utilizzi delle
 * CPU in percentuali di variazione */
void analyzer(int queue) {
    raw_msg in_message;
    delta_msg out_message;
    long int last_user = 0, last_system = 0, last_idle = 0;
    long int total;

    out_message.type = DELTA_DATA_MSG_TYPE;

    while (1) {
        if (msgrcv(queue, &in_message,
                   sizeof(in_message) - sizeof(in_message.type),
                   RAW_DATA_MSG_TYPE, 0) == -1)
            perror("msgrcv");

        if (in_message.user < 0)
            break;

        if (last_user != 0) {
            total = (in_message.user - last_user) +
                    (in_message.system - last_system) +
                    (in_message.idle - last_idle);

            out_message.user =
                (float)(in_message.user - last_user) / (float)total;
            out_message.system =
                (float)(in_message.system - last_system) / (float)total;

            if (msgsnd(queue, &out_message,
                       sizeof(out_message) - sizeof(out_message.type), 0) == -1)
                perror("msgsnd");
        }
        last_user = in_message.user;
        last_system = in_message.system;
        last_idle = in_message.idle;
    }

    /* invia, a sua volta,  un messaggio di EOF con dati negativi */
    out_message.user = out_message.system = -1.0;
    if (msgsnd(queue, &out_message,
               sizeof(out_message) - sizeof(out_message.type), 0) == -1)
        perror("msgsnd");

    exit(0);
}

void print_chars(unsigned int n, char c) {
    for (unsigned int i = 0; i < n; i++)
        putc(c, stdout);
}

/* il figlio plotter che si occupa di visualizzare i dati di variazione */
void plotter(int queue) {
    delta_msg in_message;
    unsigned int user_chars, system_chars, idle_chars;

    while (1) {
        if (msgrcv(queue, &in_message,
                   sizeof(in_message) - sizeof(in_message.type),
                   DELTA_DATA_MSG_TYPE, 0) == -1)
            perror("msgrcv");

        if (in_message.user < 0)
            break;

        user_chars = in_message.user * DISPLAY_COLUMNS;
        system_chars = in_message.system * DISPLAY_COLUMNS;
        idle_chars = DISPLAY_COLUMNS - user_chars - system_chars;

        print_chars(system_chars, '#');
        print_chars(user_chars, '*');
        print_chars(idle_chars, '_');
        printf(" s:%5.2f%% u:%5.2f%%\n", in_message.system * 100,
               in_message.user * 100);
    }

    exit(0);
}

int main(int argc, char **argv) {
    int queue;
    int num_samples;

    if ((argc == 1) || ((num_samples = atoi(argv[1])) < 2))
        num_samples = DEFAULT_NUM_SAMPLES;

    if ((queue = msgget(IPC_PRIVATE, IPC_CREAT | 0600)) == -1) {
        perror("msgget");
        exit(1);
    }

    if (fork() == 0)
        sampler(queue, num_samples);
    if (fork() == 0)
        analyzer(queue);
    if (fork() == 0)
        plotter(queue);

    wait(NULL);
    wait(NULL);
    wait(NULL);

    msgctl(queue, IPC_RMID, NULL);

    exit(0);
}
