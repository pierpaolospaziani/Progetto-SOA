#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <asm/io.h>
#include <linux/syscalls.h>
#include <linux/pid.h>
#include <linux/tty.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/delay.h>

#include "utils.c"
#include "utils_header.h"


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char*, source, size_t, size) {
#else
asmlinkage int sys_put_data(char* source, size_t size) {
#endif
    
    int ret, len, invalidBlockIndex;

    char *buffer;
    char end_str = '\0';

    struct buffer_head *bh = NULL;
    struct Block *newBlock;

    printk("%s: sys_put_data() called ...\n", MODNAME);

    // sanity check
    if (source == NULL || size >= DEFAULT_BLOCK_SIZE - METADATA_SIZE){
        ret = -EINVAL;
        goto exit;
    }

    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);
    
    // allocation of kernel buffer for the message
    buffer = kmalloc(size+1, GFP_KERNEL);
    if (!buffer) {
        printk(KERN_CRIT "%s: buffer kmalloc error\n", MODNAME);
        ret = -ENOMEM;
        goto exit;
    }

    // copies the user buffer into the kernel buffer
    ret = copy_from_user(buffer, source, size);
    len = strlen(buffer);
    if (strlen(buffer) < size)
        size = len;
    buffer[size] = end_str;

    // initializes the new block to be inserted
    newBlock = kmalloc(sizeof(struct Block), GFP_KERNEL);
    if (newBlock == NULL) {
        printk(KERN_CRIT "%s: newBlock kmalloc error\n", MODNAME);
        ret = -ENOMEM;
        goto exit;
    }
    newBlock->isValid = true;
    newBlock->nextInvalidBlock = -1;
    memcpy(newBlock->data, buffer, size+1);

    // searches for an overwritable block
    invalidBlockIndex = getInvalidBlockIndex();
    if (invalidBlockIndex == -1){
        printk(KERN_CRIT "%s: no block available\n", MODNAME);
        ret = -ENOMEM;
        goto exit;
    }

    // attesa della fine del grace period
    // synchronize_srcu(&(au_info.srcu));

    // prendi il lock in scrittura per la concorrenza con invalidate_data
    // mutex_lock(&(rcu.write_lock));
        
    // gets the buffer_head
    bh = sb_bread(superblock, invalidBlockIndex);
    if (!bh) {
        printk(KERN_CRIT "%s: buffer_head read error\n", MODNAME);
        ret = -EIO;
        // mutex_unlock(&(rcu.write_lock));    // <---
        goto exit;
    }

    // updates 'firstInvalidBlock' with the value of 'nextInvalidBlock' of the selected block
    ret = updateSuperblockInvalidEntry(((struct Block *) bh->b_data)->nextInvalidBlock);
    if (ret < -2) {
        printk(KERN_CRIT "%s: an error occurred during the superblock update\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    if (bh->b_data == NULL) {
        printk(KERN_CRIT "%s: buffer_head data error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    // writing the data
    memcpy(bh->b_data, (char *) newBlock, sizeof(struct Block));
    mark_buffer_dirty(bh);

    // if 'SYNCHRONUS_WRITE_BACK' is not defined, write operations will be executed by the page-cache write back daemon
    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) == 0)
        printk("%s: synchronous writing successful", MODNAME);
    else
        printk(KERN_CRIT "%s: synchronous writing failed", MODNAME);
    #endif

    brelse(bh);
    ret = invalidBlockIndex;

    // mutex_unlock(&(rcu.write_lock));

exit:
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);
    // wake_up_interruptible(&unmount_wq);
    kfree(buffer);
    kfree(newBlock);
    return ret;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char*, destination, size_t, size) {
#else
asmlinkage int sys_get_data(int offset, char* destination, size_t size) {
#endif

    int ret, blocksNumber;
    char end_str = '\0';
    // unsigned long my_epoch;
    struct onefilefs_sb_info *superblockInfo;
    struct buffer_head *bh = NULL;
    struct Block *block;

    printk("%s: get_data called ...", MODNAME);

    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);

    // retrieve the blocksNumber
    bh = sb_bread(superblock, 0);
    if(!bh){
        printk(KERN_CRIT "%s: buffer_head read error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }
    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;
    blocksNumber = superblockInfo->blocksNumber;
    brelse(bh);

    // sanity check
    if (offset < 0 || offset >= blocksNumber-2 || size < 0 || destination == NULL){
        printk(KERN_CRIT "%s: sys_get_data() error: invalid input!\n", MODNAME);
        ret = -EINVAL;
        goto exit;
    }
    size = (size >= DEFAULT_BLOCK_SIZE - METADATA_SIZE) ? (DEFAULT_BLOCK_SIZE - METADATA_SIZE) : size;

    // segnala la presenza del reader per evitare che uno scrittore riutilizzi lo stesso blocco mentre lo si sta leggendo
    // my_epoch = __sync_fetch_and_add(&(rcu.epoch),1);

    // gets the buffer_head
    bh = (struct buffer_head *) sb_bread(superblock, offset+2);
    if (!bh) {
        printk(KERN_CRIT "%s: buffer_head read error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    if (bh->b_data == NULL) {
        printk(KERN_CRIT "%s: buffer_head data error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    // check if the block is valid
    block = (struct Block *) bh->b_data;
    if (!block->isValid){
        printk(KERN_CRIT "%s: the requested block is invalid\n", MODNAME);
        ret = -ENODATA;
        goto exit;
    }

    // if the requested size is too large, the complete message is returned
    ret = strlen(block->data);
    if (size > ret)
        size = ret;

    // copy the data to the destination buffer
    ret = copy_to_user(destination, block->data, size);
    if (ret != 0){
        printk(KERN_CRIT "%s: copy_to_user error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }
    ret = copy_to_user(destination+size, &end_str, 1);
    if (ret != 0){
        printk(KERN_CRIT "%s: copy_to_user error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    ret = size+1;

    printk("%s: %d bytes loaded\n", MODNAME, ret);

    brelse(bh);

exit:
    // // the first bit in my_epoch is the index where we must release the counter
    // index = (my_epoch & MASK) ? 1 : 0;
    // __sync_fetch_and_add(&(rcu.standing[index]),1);
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);
    // wake_up_interruptible(&readers_wq);
    // wake_up_interruptible(&unmount_wq);

    return ret;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset) {
#else
asmlinkage int sys_invalidate_data(int offset) {
#endif

    int ret = 0, blocksNumber;
    
    struct onefilefs_sb_info *superblockInfo;
    struct buffer_head *bh;
    struct Block *block;

    printk("%s: invalidate_data called ...\n", MODNAME);

    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);

    // retrieve the blocksNumber
    bh = sb_bread(superblock, 0);
    if(!bh){
        return -EIO;
    }
    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;
    blocksNumber = superblockInfo->blocksNumber;
    brelse(bh);

    // sanity check
    if (offset < 0 || offset >= blocksNumber-2) return -EINVAL;

    // prendi il lock in scrittura per la concorrenza con put_data ed altre invalidate_data
    // mutex_lock(&(rcu.write_lock)); 

    // // move to a new epoch
    // updated_epoch = (rcu.next_epoch_index) ? MASK : 0;
    // rcu.next_epoch_index += 1;
    // rcu.next_epoch_index %= 2;  

    // last_epoch = __atomic_exchange_n (&(rcu.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    // index = (last_epoch & MASK) ? 1 : 0; 
    // grace_period_threads = last_epoch & (~MASK); 
    
    // wait_event_interruptible(readers_wq, rcu.standing[index] >= grace_period_threads);
    // rcu.standing[index] = 0;

    // gets the buffer_head
    bh = (struct buffer_head *) sb_bread(superblock, offset+2);
    if (!bh) {
        printk(KERN_CRIT "%s: buffer_head read error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    if (bh->b_data == NULL) {
        printk(KERN_CRIT "%s: buffer_head data error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    // flags the block as invalid
    block = (struct Block *) bh->b_data;
    if (!block->isValid){
        printk(KERN_CRIT "%s: block %d is already invalid\n", MODNAME, offset+2);
        ret = -ENODATA;
        goto exit;
    }

    // updates the superblock metadata by placing the newly invalidated block as the first of the invalidated blocks
    ret = updateSuperblockInvalidEntry(offset+2);
    if (ret < -2) {
        printk(KERN_CRIT "%s: error updating superblock\n", MODNAME);
        ret = -ENODEV;
        goto exit;
    }
    block->isValid = false;
    block->nextInvalidBlock = ret;
    mark_buffer_dirty(bh);

    // if 'SYNCHRONUS_WRITE_BACK' is not defined, write operations will be executed by the page-cache write back daemon
    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) == 0)
        printk("%s: synchronous writing successful", MODNAME);
    else
        printk(KERN_CRIT "%s: synchronous writing failed", MODNAME);
    #endif

    brelse(bh);
    
    printk("%s: block %d has been invalidated\n", MODNAME, offset+2);

    ret = offset+2;

exit:
    // mutex_unlock(&(rcu.write_lock));
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);
    // wake_up_interruptible(&unmount_wq);
    return ret;
}



#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)       
unsigned long sys_put_data = (unsigned long) __x64_sys_put_data;
unsigned long sys_get_data = (unsigned long) __x64_sys_get_data;
unsigned long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif