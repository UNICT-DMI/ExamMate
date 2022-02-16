/* Sistemi Operativi - prova di laboratorio
   compito del 01/07/2015 - soluzione       */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>

#define TEXT_DIM 900
#define BUFFER_SIZE 4096
#define DEST_PARENT 1000

/* enumerazione dei possibili payload dei messaggi */
typedef enum {
    CMD_LIST,
    CMD_SIZE,
    CMD_SEARCH,
    CMD_EXIT,
    REPLY_ERROR,
    REPLY_DATA,
    REPLY_DATA_STOP
} type_payload;

/* la struttura di un messaggio scambiato tra i processi: */
typedef struct {
    long type;              // indicazione del destinatario: 1...n ai figli, DEST_PARENT=1000 per il padre
    type_payload payload;   // il payload del messaggio: un comando, la risposta/e o una segnalazione di errore
    char text[TEXT_DIM];    // un stringa o
    long number;            // un numero
} msg;

/* il generico figlio D-n che gestisce l'esecuzione delle richieste sulla propria directory */
void child_dir(int num, char *dir, int queue) {
    msg message;
    struct stat statbuf;
    DIR *dp;
    struct dirent *entry;
    FILE *file;
    char buffer[BUFFER_SIZE];
    int occurrences;
    char done = 0;

    printf("figlio D-%d su directory '%s' avviato\n", num, dir);
    
    /* si sposta nella directory da gestire */
    if (chdir(dir) == -1) {
        perror(dir);
        exit(1);
    }
    do {
        /* riceve il messaggio con il comando da eseguire */
        if (msgrcv(queue, &message, sizeof(msg) - sizeof(long), num, 0) == -1) {
            perror("msgrcv");
            exit(1);
        }
        
        switch(message.payload) {
            case CMD_LIST:
                /* legge il contenuto della directory corrente */
                dp = opendir(".");
                message.type = DEST_PARENT;
                while ((entry=readdir(dp)) != NULL) {
                    lstat(entry->d_name, &statbuf);
                    if (S_ISREG(statbuf.st_mode)) {
                        /* invia un messaggio per ogni file */
                        message.payload = REPLY_DATA;
                        strncpy(message.text, entry->d_name, TEXT_DIM);
                        msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                    }
                }
                /* invia un messaggio finale di tipo REPLY_DATA_STOP
                   per segnalare la fine della lista                 */
                message.payload = REPLY_DATA_STOP;
                msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                closedir(dp);
                break;
                
            case CMD_SIZE:
                message.type = DEST_PARENT;
                if ((lstat(message.text, &statbuf) == -1) || (!S_ISREG(statbuf.st_mode))) {
                    message.payload = REPLY_ERROR;
                    msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                    break;
                }
                /* invia un messaggio con la dimensione in byte */
                message.payload = REPLY_DATA;
                message.number = statbuf.st_size;
                msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                break;
                
            case CMD_SEARCH:
                /* si fanno una serie di controlli sul file e
                   poi si tenta di aprirlo in lettura         */
                if ((lstat(message.text, &statbuf) == -1) || (!S_ISREG(statbuf.st_mode)) || ((file = fopen(message.text, "r")) == NULL)) {
                    /* in caso di problemi si invia un messaggio di tipo REPLY_ERROR */
                    message.type = DEST_PARENT;
                    message.payload = REPLY_ERROR;
                    msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                    
                    /* si estrae il secondo messaggio per pulire la coda */
                    msgrcv(queue, &message, sizeof(msg) - sizeof(long), num, 0);
                    break;
                }
                
                /* riceve il secondo messaggio di tipo CMD_SEARCH
                   con la parola da cercare                       */
                msgrcv(queue, &message, sizeof(msg) - sizeof(long), num, 0);
                occurrences = 0;
                while (fgets(buffer, BUFFER_SIZE, file))
                    if (strstr(buffer, message.text))
                        occurrences++;
                
                /* invia un messaggio con il numero di occorrenze */
                message.type = DEST_PARENT;
                message.payload = REPLY_DATA;
                message.number = occurrences;
                msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                break;
                
            case CMD_EXIT:
                done = 1;
                break;
        }
    } while (!done);
    
    printf("figlio D-%d terminato\n", num);
    exit(0);
}

int main(int argc, char **argv) {
    int queue;
    int i, len, children;
    struct stat statbuf;
    char buffer[BUFFER_SIZE];
    msg message;
    char *token;
    char done = 0;

    if (argc <= 1) {
        fprintf(stderr, "uso: %s <directory 1> <directory 2> ...\n", argv[0]);
        exit(1);
    }

    if ((queue = msgget(IPC_PRIVATE, IPC_CREAT|0600)) == -1) {
        perror("msgget");
        exit(1);
    }

    /* crea i processi figli necessari */
    children = 0;
    for (i = 1; i < argc; i++) {
        if ((stat(argv[i], &statbuf) == -1) || (!S_ISDIR(statbuf.st_mode))) {
            fprintf(stderr, "sembra esserci qualche problema con il parametro '%s': lo salto!\n", argv[i]);
            continue;
        }

        children++;
        if (fork() == 0)
            child_dir(i, argv[i], queue);
    }
    if (children == 0) {
        fprintf(stderr, "nessuno dei parametri passati era valido!\n");
        exit(1);
    }

    sleep(1);
    do {
        printf("file-shell> ");
        fgets(buffer, BUFFER_SIZE, stdin);
        len = strlen(buffer);
        if (buffer[len-1] == '\n')
            buffer[len-1] = '\0';
        
        if (token = strtok(buffer, " ")) {  // estrae il primo token: il comando
            if (strcmp(token, "list") == 0) {
                if (token = strtok(NULL, " ")) {    // estrae il secondo token: il numero del figlio
                    i = atoi(token);
                    if ((i >= 1) && (i <= children)) {
                        /* invia un messaggio al figlio indicato con la richista CMD_LIST */
                        message.type = i;
                        message.payload = CMD_LIST;
                        msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                        
                        /* riceve una serie di messaggi (uno per file) fino a REPLY_DATA_STOP */
                        while (1){
                            msgrcv(queue, &message, sizeof(msg) - sizeof(long), DEST_PARENT, 0);
                            if (message.payload == REPLY_ERROR) {
                                fprintf(stderr, "P - errore dal figlio\n");
                                break;
                            } else if (message.payload == REPLY_DATA_STOP)
                                break;
                            printf("\t%s\n", message.text);
                        }
                    }
                }
            } else if (strcmp(token, "size") == 0) {
                if (token = strtok(NULL, " ")) {    // estrae il secondo token: il numero del figlio
                    i = atoi(token);
                    if ((i >= 1) && (i <= children)) {
                        message.type = i;
                        if (token = strtok(NULL, " ")) {    // estrae il terzo token: il nome del file
                            /* invia un messaggio al figlio indicato con la richista CMD_SIZE */
                            strncpy(message.text, token, TEXT_DIM);
                            message.payload = CMD_SIZE;
                            msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);

                            /* riceve un messaggio con la risposta */
                            msgrcv(queue, &message, sizeof(msg) - sizeof(long), DEST_PARENT, 0);
                            if (message.payload == REPLY_ERROR)
                                fprintf(stderr, "P - errore dal figlio\n");
                            else
                                printf("\t%ld byte\n", message.number);
                        }
                    }
                }
            } else if (strcmp(token, "search") == 0) {
                if (token = strtok(NULL, " ")) {    // estrae il secondo token: il numero del figlio
                    i = atoi(token);
                    if ((i >= 1) && (i <= children)) {
                        message.type = i;
                        if (token = strtok(NULL, " ")) {    // estrae il terzo token: il nome del file
                            /* invierà due messaggi di tipo CMD_SEARCH:
                               uno con il nome del file e un altro 
                               con la parola da cercare
                               
                               qui viene intanto preparato il primo     */
                            strncpy(message.text, token, TEXT_DIM);
                            message.payload = CMD_SEARCH;
                            if (token = strtok(NULL, " ")) {    // estrae il quarto token: la parola da cercare
                                /* invia il primo messaggio solo dopo essere sicuri di avere la parola */
                                msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);
                                
                                /* crea ed invia il secondo messaggio */
                                strncpy(message.text, token, TEXT_DIM);
                                message.payload = CMD_SEARCH;
                                msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0);

                                /* riceve un messaggio con la risposta */
                                msgrcv(queue, &message, sizeof(msg) - sizeof(long), DEST_PARENT, 0);
                                if (message.payload == REPLY_ERROR)
                                    fprintf(stderr, "P - errore dal figlio\n");
                                else
                                    printf("\t%ld occorrenza/e\n", message.number);
                            }
                        }
                    }
                }
            } else if ((strcmp(token, "exit") == 0) || (strcmp(token, "quit") == 0)) {
                done = 1;
            } else
                fprintf(stderr, "P - comando '%s' non riconosciuto!\n", token);
        }
    } while (!done);
    
    /* invia una serie di messaggi di tipo CMD_EXIT
       a tutti i figli e ne aspetta la terminazione */
    for (i = 1; i <= children; i++) {
        message.type = i;
        message.payload = CMD_EXIT;
        if (msgsnd(queue, &message, sizeof(msg) - sizeof(long), 0) == -1)
            perror("msgsnd");
    }
    for (i = 1; i <= children; i++)
        wait(NULL);
    msgctl(queue, IPC_RMID, NULL);
}
