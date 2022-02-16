/* Sistemi Operativi - prova di laboratorio
 *  compito del 13/07/2016 - soluzione       */

/* serve per utilizzare la funzione non-POSIX strcasestr() */
#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define FIFO_PATHNAME_TEMPLATE "/tmp/my_fgrep_%d.fifo"
#define MAX_LEN_BUFFER 2048

void reader(const char *input, int pipe_fd) {
    FILE *input_fs, *output_fs;
    char buffer[MAX_LEN_BUFFER];

    if (input == NULL)
        input_fs = stdin;
    else if ((input_fs = fopen(input, "r")) == NULL) {
        perror(input);
        exit(1);
    }

    if ((output_fs = fdopen(pipe_fd, "w")) == NULL) {
        perror("pipe");
        exit(1);
    }

    while (fgets(buffer, MAX_LEN_BUFFER, input_fs) != NULL)
        fputs(buffer, output_fs);

    exit(0);
}

void filterer(int pipe_fd, const char *output_fifo, const char *word,
              char case_insensitive, char inverse_logic) {
    FILE *input_fs, *output_fs;
    char buffer[MAX_LEN_BUFFER];

    if ((input_fs = fdopen(pipe_fd, "r")) == NULL) {
        perror("pipe");
        exit(1);
    }

    if ((output_fs = fopen(output_fifo, "w")) == NULL) {
        perror(output_fifo);
        exit(1);
    }

    while (fgets(buffer, MAX_LEN_BUFFER, input_fs) != NULL) {
        char match;

        if (case_insensitive)
            match = (strcasestr(buffer, word) != NULL);
        else
            match = (strstr(buffer, word) != NULL);

        if ((inverse_logic && !match) || (!inverse_logic && match))
            fputs(buffer, output_fs);
    }
    exit(0);
}

void writer(const char *input_fifo) {
    FILE *input_fs;
    char buffer[MAX_LEN_BUFFER];

    if ((input_fs = fopen(input_fifo, "r")) == NULL) {
        perror(input_fifo);
        exit(1);
    }

    while (fgets(buffer, MAX_LEN_BUFFER, input_fs) != NULL)
        fputs(buffer, stdout);

    exit(0);
}

int main(int argc, char **argv) {
    char case_insensitive = 0;
    char inverse_logic = 0;
    char *word = NULL;
    char *input = NULL;
    struct stat sb;
    int pipe_fds[2];
    char fifo_pathname[MAX_LEN_BUFFER];

    /* analizza la command-line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            case_insensitive = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            inverse_logic = 1;
        } else if (strlen(argv[i]) > 0) {
            if (word == NULL) {
                word = argv[i];
            } else {
                if ((stat(argv[i], &sb) == 0) && (S_ISREG(sb.st_mode)))
                    input = argv[i];
                else {
                    perror(argv[i]);
                    exit(1);
                }
            }
        }
    }

    if (word == NULL) {
        printf("use: my-fgrep [-i] [-v] <word> [file]\n");
        exit(1);
    }

    if (pipe(pipe_fds) == -1) {
        perror("pipe");
        exit(1);
    }

    /* utilizza nel nome della FIFO sul file-system il PID del
     * processo padre: questo permette di usare FIFO diverse per
     * istanze multiple del programma; usando la stessa fifo l'ultimo
     * esempio nel testo del compito non funzionerebbe...               */
    snprintf(fifo_pathname, MAX_LEN_BUFFER, FIFO_PATHNAME_TEMPLATE, getpid());

    /* controllo se la FIFO sul file-system esiste gia',
     * eventualmente la crea                                */
    if (stat(fifo_pathname, &sb) == -1) {
        if (!S_ISFIFO(sb.st_mode))
            unlink(fifo_pathname);
        if (mkfifo(fifo_pathname, 0600) == -1) {
            perror(fifo_pathname);
            exit(1);
        }
    }

    if (fork() == 0) {
        /* chiude il canale di input sulla pipe nel figlio reader */
        close(pipe_fds[0]);
        reader(input, pipe_fds[1]);
    }

    if (fork() == 0) {
        /* chiude il canale di output sulla pipe nel figlio filterer */
        close(pipe_fds[1]);
        filterer(pipe_fds[0], fifo_pathname, word, case_insensitive,
                 inverse_logic);
    }

    /* chiude entrambi i canali sulla pipe: non servono ne' al figlio
     * writer, ne' al padre; mantenerli aperti non permetterebbe la notifica
     * dell'EOF al figlio filterer                                           */
    close(pipe_fds[0]);
    close(pipe_fds[1]);

    if (fork() == 0)
        writer(fifo_pathname);

    wait(NULL);
    wait(NULL);
    wait(NULL);

    if (unlink(fifo_pathname) == -1) {
        perror(fifo_pathname);
        exit(1);
    }
    exit(0);
}
