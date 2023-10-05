#ifndef _UTILS_H
#define _UTILS_H

#include <linux/version.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>

#include "filesystem/singlefilefs.h"

#define SYNCHRONUS_WRITE_BACK   // if commented, write operations will be executed by the page-cache write back daemon

#define IMAGE_PATH "image"

#define MODNAME "MY_MODULE"

#define DEVICE_NAME "my_device"

// RCU
#define PERIOD 30
#define EPOCHS 2
#define MASK 0x8000000000000000

struct rcu_counter {
    unsigned long standing[EPOCHS];
    unsigned long epoch;
    int next_epoch_index;
    struct mutex write_lock;
} __attribute__((packed));



extern struct rcu_counter rcu;                      // rcu struct  
// extern wait_queue_head_t unmount_wq;                // wait queue per lo smontaggio del filesystem   
// extern wait_queue_head_t readers_wq;                // wait queue per l'attesa dei reader (RCU)

#endif