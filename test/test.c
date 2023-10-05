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


void put_data(int number) {
    
    // syscall(PUT_DATA, "source", 0);
    
    int ret;
    char source[DEFAULT_BUFFER_SIZE];
    size_t size;
    
    sprintf(source, "Questo è un esempio di stringa: %d", number);
    size = strlen(source);

    do {
        ret = syscall(PUT_DATA, source, size);
    } while(errno == EAGAIN);

    if(ret >= 0) {
        printf("Esecuzione test_put_syscall() terminata con successo e scritto il blocco %d\n", ret);
        fflush(stdout);
    }
    else {
        printf("ERROR: test_put_syscall() failed\n");
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
        printf("ERROR: test_get_syscall() failed\n");
        fflush(stdout);
    }
}

void invalidate_data(int offset) {

    int ret;

    ret = syscall(INVALIDATE_DATA, offset);
    
    if(ret >= 0) {
        printf("Esecuzione test_invalidate_syscall() terminata con successo, invalidato il blocco %d\n", offset+2);
        fflush(stdout);
    }
    else {
        printf("ERROR: test_invalidate_syscall() failed\n");
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Utilizzo: %s <operation>\n", argv[0]);
        return 1;
    }

    int operation = atoi(argv[1]);
    int value = atoi(argv[2]);

    switch (operation) {
        case 1:
            put_data(value);
            break;
        case 2:
            get_data(value);
            break;
        case 3:
            invalidate_data(value);
            break;
        default:
            fprintf(stderr, "ERROR: invalid operation: %d\n", operation);
            return 1;
    }

    return 0;
}