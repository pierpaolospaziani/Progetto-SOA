#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>

#include "../defines.h"

#define PUT_DATA         134
#define GET_DATA         156
#define INVALIDATE_DATA  174

#define DEFAULT_BUFFER_SIZE 128

#define NBLOCKS 4

#define NTHREADS 10

pthread_barrier_t barrier;

void *put_data(void *arg) {
        
    int ret, number;
    char source[DEFAULT_BUFFER_SIZE];
    size_t size;

    number = *(int*)arg;
    
    sprintf(source, "Questo è un esempio di stringa: %d", number);
    size = strlen(source);

    pthread_barrier_wait(&barrier);

    #ifdef TEST_RCU
    usleep(1000000);
    #endif

    ret = syscall(PUT_DATA, source, size);

    if(ret >= 0) {
        printf("Thread %d wrote on block %d\n", number, ret);
        fflush(stdout);
    } else {
        perror("ERROR: put_syscall() failed");
    }
}

void *get_data(void *arg) {

    int ret, offset;
    char destination[DEFAULT_BUFFER_SIZE];

    offset = *(int*)arg;

    pthread_barrier_wait(&barrier);

    ret = syscall(GET_DATA, offset, destination, DEFAULT_BUFFER_SIZE);
    
    if(ret >= 0) {
        printf("[Block %d]: %s\n", offset, destination);
        fflush(stdout);
    } else {
        perror("ERROR: get_syscall() failed");
    }
}

void *invalidate_data(void *arg) {

    int ret, offset;

    offset = *(int*)arg;

    pthread_barrier_wait(&barrier);

    #ifdef TEST_RCU
    usleep(1000000);
    #endif

    ret = syscall(INVALIDATE_DATA, offset);
    
    if(ret >= 0) {
        printf("Block %d invalidated\n", offset+2);
        fflush(stdout);
    } else {
        perror("ERROR: invalidate_syscall() failed");
    }
}

int main(int argc, char *argv[]) {

    int thread_index, ret, params[NTHREADS];
    pthread_t *tids;
    pthread_barrier_init(&barrier, NULL, NTHREADS);

    tids = malloc(NTHREADS*sizeof(pthread_t));
    if (!tids) {
        printf("ERROR: malloc failed\n");
        fflush(stdout);
        return -1;      
    }

    if (argc < 3) {
        fprintf(stderr, "Utilizzo: %s <operation>\n", argv[0]);
        return 1;
    }

    int operation = atoi(argv[1]);
    int value = atoi(argv[2]);

    srand(time(NULL));

    for(thread_index=0; thread_index<NTHREADS; thread_index++) {

        params[thread_index] = thread_index;
        
        switch (operation) {
            case 1:
                ret = pthread_create(&tids[thread_index], NULL, put_data, (void*)&params[thread_index]);
                break;
            case 2:
                if (thread_index == 0)
                    ret = pthread_create(&tids[thread_index], NULL, get_data, (void*)&value);
                break;
            case 3:
                if (thread_index == 0)
                    ret = pthread_create(&tids[thread_index], NULL, invalidate_data, (void*)&value);
                break;
            case 4:
                value = random() % NBLOCKS;
                if (thread_index%2)
                    ret = pthread_create(&tids[thread_index], NULL, put_data, (void*)&params[thread_index]);
                    // ret = pthread_create(&tids[thread_index], NULL, invalidate_data, (void*)&value);

                else
                    ret = pthread_create(&tids[thread_index], NULL, get_data, (void*)&value);
                break;
            default:
                fprintf(stderr, "ERROR: invalid operation: %d\n", operation);
                pthread_barrier_destroy(&barrier);
                return 1;
        }
        if (ret != 0) {
            printf("ERROR: thread creation failed\n");
            fflush(stdout);
            free(tids);
            pthread_barrier_destroy(&barrier);
            return -1;
        }
    }
    for(thread_index=0; thread_index<NTHREADS; thread_index++) {
        pthread_join(tids[thread_index], NULL);
    }
    pthread_barrier_destroy(&barrier);
    return 0;
}