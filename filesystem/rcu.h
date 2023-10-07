#ifndef _RCU_H
#define _RCU_H

#include <linux/mutex.h>

#define EPOCHS 2

// rcu metadata definition
struct rcu_metadata {
    unsigned long readers[EPOCHS];
    unsigned long epoch;
    struct mutex write_lock;
} __attribute__((packed));

extern struct rcu_metadata rcu;
extern wait_queue_head_t rcu_wq;

#endif
