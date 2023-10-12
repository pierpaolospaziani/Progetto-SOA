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

#define DEFAULT_BUFFER_SIZE 4084

pthread_barrier_t barrier;

void *put_data(void *arg) {
        
    int ret;
    char source[DEFAULT_BUFFER_SIZE];
    size_t size;
    pthread_t tid;

    tid = *(pthread_t *)arg;
    
    sprintf(source, "Thread %ld wrote this", tid);
    size = strlen(source);

    pthread_barrier_wait(&barrier);

    #ifdef TEST_RCU
    usleep(1000000);
    #endif

    ret = syscall(PUT_DATA, source, size);

    if(ret >= 0) {
        printf("[THREAD %ld] PUT - Wrote on block %d\n", tid, ret-2);
        fflush(stdout);
    } else {
        fprintf(stderr, "[THREAD %ld] PUT ERROR: %s\n",tid, strerror(errno));
    }
}

void *get_data(void *arg) {

    int ret, offset;
    char destination[DEFAULT_BUFFER_SIZE];
    pthread_t tid;

    tid = *(pthread_t *)arg;

    pthread_barrier_wait(&barrier);

    offset = tid%(NBLOCKS-2);

    ret = syscall(GET_DATA, offset, destination, DEFAULT_BUFFER_SIZE);
    
    if(ret >= 0) {
        printf("[THREAD %ld] GET - Block %d: %s\n", tid, offset, destination);
        fflush(stdout);
    } else {
        fprintf(stderr, "[THREAD %ld] GET ERROR: %s\n",tid, strerror(errno));
    }
}

void *invalidate_data(void *arg) {

    int ret, offset;
    pthread_t tid;

    tid = *(pthread_t *)arg;

    pthread_barrier_wait(&barrier);

    offset = tid%(NBLOCKS-2);

    #ifdef TEST_RCU
    usleep(1000000);
    #endif

    ret = syscall(INVALIDATE_DATA, offset);
    
    if(ret >= 0) {
        printf("[THREAD %ld] INV - Block %d invalidated\n", tid, offset);
        fflush(stdout);
    } else {
        fprintf(stderr, "[THREAD %ld] INV ERROR: %s\n",tid, strerror(errno));
    }
}

void *cat(void *arg) {

    pthread_t tid;
    int ret;
    char cwd[1024];

    tid = *(pthread_t *)arg;

    pthread_barrier_wait(&barrier);

    if (strstr(getcwd(cwd, sizeof(cwd)), "test") != NULL)
        ret = system("cat ../mount/the-file");
    else
        ret = system("cat mount/the-file");

    if (ret != 0)
        fprintf(stderr, "[THREAD %ld] CAT ERROR: %s\n",tid, strerror(errno));
}

int main(int argc, char *argv[]) {

    int thread_index, ret;
    pthread_t *tids;
    pthread_barrier_init(&barrier, NULL, NTHREADS);

    tids = malloc(NTHREADS*sizeof(pthread_t));
    if (!tids) {
        printf("ERROR: malloc failed\n");
        fflush(stdout);
        return -1;      
    }

    srand(time(NULL));

    for(thread_index=0; thread_index<NTHREADS; thread_index++) {
        switch (random() % 3) {
            case 0:
                ret = pthread_create(&tids[thread_index], NULL, put_data, &tids[thread_index]);
                break;
            case 1:
                ret = pthread_create(&tids[thread_index], NULL, get_data, &tids[thread_index]);
                break;
            case 2:
                ret = pthread_create(&tids[thread_index], NULL, invalidate_data, &tids[thread_index]);
                break;
            case 3:
                ret = pthread_create(&tids[thread_index], NULL, cat, &tids[thread_index]);
                break;
            default:
                printf("ERROR: invalid operation\n");
                fflush(stdout);
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