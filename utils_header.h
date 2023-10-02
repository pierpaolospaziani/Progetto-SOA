#ifndef _UTILS_H
#define _UTILS_H

#include <linux/version.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>

#include "common_header.h"
#include "filesystem/singlefilefs.h"

#define SYNC_WRITE_BACK     // comment this line to disable synchronous writing

// BLOCK LEVEL DATA MANAGEMENT SERVICE STUFF
#define MODNAME "MY_MODULE"
#define DEVICE_NAME "my_device"
#define DEV_NAME "./mount/the-file"

// RCU STUFF
#define PERIOD 30
#define EPOCHS 2
#define MASK 0x8000000000000000

// MAJOR & MINOR UTILS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session) MAJOR(session->f_inode->i_rdev)
#define get_minor(session) MINOR(session->f_inode->i_rdev)
#else
#define get_major(session) MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session) MINOR(session->f_dentry->d_inode->i_rdev)
#endif



/* KERNEL METADATA TO MANAGE MESSAGES */

// Variable accessed by the mount thread, readers and writers
struct block_device_metadata {
    unsigned int usage;
    struct block_device *bdev;
};

// Variable accessed only by the mount thread
struct filesystem_metadata {
    bool isMounted;
    atomic_t currentlyInUse;
    char deviceName[20];
};


// RCU counter
struct rcu_counter {
    unsigned long standing[EPOCHS];
    unsigned long epoch;
    int next_epoch_index;
    struct mutex write_lock;
} __attribute__((packed));



extern struct block_device_metadata bd_metadata;    // block device metadata
extern struct rcu_counter rcu;                      // rcu struct  
// extern wait_queue_head_t unmount_wq;                // wait queue per lo smontaggio del filesystem   
// extern wait_queue_head_t readers_wq;                // wait queue per l'attesa dei reader (RCU)

#endif