#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#define PUT_DATA         134
#define GET_DATA         156
#define INVALIDATE_DATA  174

#define DEFAULT_BUFFER_SIZE 128


void put_data() {
    
    // syscall(PUT_DATA, "source", 0);
    
    int ret;
    char source[DEFAULT_BUFFER_SIZE];
    size_t size;
    
    strcpy(source, "Questo è un esempio di stringa in C");
    size = strlen(source);

    do {
        ret = syscall(PUT_DATA, source, size);
    } while(errno == EAGAIN);

    if(ret >= 0) {
        printf("Esecuzione test_put_syscall() terminata con successo e scritto il blocco %d\n", ret);
        fflush(stdout);
    }
    else {
        printf("Esecuzione test_put_syscall() fallita\n");
        fflush(stdout);
    }
}

void get_data(int offset) {
    // syscall(GET_DATA, 0, "destination", 0);

    int ret;
    char destination[DEFAULT_BUFFER_SIZE];

    ret = syscall(GET_DATA, offset, destination, DEFAULT_BUFFER_SIZE);
    
    if(ret >= 0) {
        printf("Esecuzione test_get_syscall() terminata con successo sul blocco %d - read:\n\t%s\n", offset, destination);
        fflush(stdout);
    } else {
        printf("Esecuzione test_get_syscall() fallita\n");
        fflush(stdout);
    }
}

void invalidate_data(int offset) {

    int ret;

    ret = syscall(INVALIDATE_DATA, offset);
    
    if(ret >= 0) {
        printf("Esecuzione test_invalidate_syscall() terminata con successo, invalidato il blocco %d\n", ret);
        fflush(stdout);
    }
    else {
        printf("Esecuzione test_invalidate_syscall() fallita\n");
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Utilizzo: %s <valore>\n", argv[0]);
        return 1;
    }

    int valore = atoi(argv[1]);

    switch (valore) {
        case 1:
            put_data();
            break;
        case 2:
            get_data(atoi(argv[2]));
            break;
        case 3:
            invalidate_data(atoi(argv[2]));
            break;
        default:
            fprintf(stderr, "Valore non valido: %d\n", valore);
            return 1;
    }

    return 0;
}