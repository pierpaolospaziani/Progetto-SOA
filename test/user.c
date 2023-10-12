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

#define DEFAULT_BUFFER_SIZE 4084

char menu[] = {"\n\
 +=================================================+\n\
 |      BLOCK-LEVEL DATA MANAGEMENT SERVICE        |\n\
 +-------------------------------------------------+\n\
 |      1) PUT DATA                                |\n\
 |      2) GET DATA                                |\n\
 |      3) INVALIDATE DATA                         |\n\
 |      4) EXIT                                    |\n\
 +=================================================+\n\n"};


void put_data() {

    int ret;
    char source[DEFAULT_BUFFER_SIZE];
    size_t size;

    printf("\n What do you want to write? > ");
    fgets(source, DEFAULT_BUFFER_SIZE, stdin);

    size = strlen(source);

    ret = syscall(PUT_DATA, source, size);

    if(ret >= 0) {
        printf("\n Message correctly wrote on block %d\n", ret-2);
        fflush(stdout);
    } else {
        fprintf(stderr, "\n ERROR: %s\n", strerror(errno));
    }
}

void get_data() {

    int ret, offset;
    char destination[DEFAULT_BUFFER_SIZE];
    ssize_t size;

    printf("\n Which block do you wanna read? > ");
    scanf("%d", &offset);
    while(getchar() != '\n');

    printf("\n How many bytes? > ");
    scanf("%ld", &size);
    while(getchar() != '\n');
    
    ret = syscall(GET_DATA, offset, destination, DEFAULT_BUFFER_SIZE);

    if(ret >= 0) {
        printf("\n Block %d: %s", offset, destination);
        fflush(stdout);
    } else {
        fprintf(stderr, "\n ERROR: %s\n", strerror(errno));
    }
}

void invalidate_data() {

    int ret, offset;

    printf("\n Which block do you wanna invalidate? > ");
    scanf("%d", &offset);
    while(getchar() != '\n');

    ret = syscall(INVALIDATE_DATA, offset);

    if(ret >= 0) {
        printf("\n Block %d invalidated\n", offset);
        fflush(stdout);
    } else {
        fprintf(stderr, "\n ERROR: %s\n", strerror(errno));
    }
}

int main(int argc, char *argv[]) {
    
    int op, ret;

    while (1) {
        system("clear");
        printf("%s Choose an option: > ", menu);
        scanf("%d", &op);
        while(getchar() != '\n');
        switch (op) {
            case 1:
                put_data();
                printf("\n [Press ENTER to go back]");
                while(getchar() != '\n');
                break;
            case 2:
                get_data();
                printf("\n [Press ENTER to go back]");
                while(getchar() != '\n');
                break;
            case 3:
                invalidate_data();
                printf("\n [Press ENTER to go back]");
                while(getchar() != '\n');
                break;
            case 4:
                return 0;
            default:
                break;
        }
    }
    return 0;
}