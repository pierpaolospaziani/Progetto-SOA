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
#include "defines.h"
#include "filesystem/rcu.h"


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char*, source, size_t, size) {
#else
asmlinkage int sys_put_data(char* source, size_t size) {
#endif
    
    int ret, len, invalidBlockIndex, oldEpoch;
    char *buffer;
    struct buffer_head *bh = NULL;
    struct Block *newBlock;

    printBlocks();

    printk("%s: sys_put_data() called ...\n", MODNAME);

    // sanity check
    if (source == NULL || size >= DEFAULT_BLOCK_SIZE - METADATA_SIZE){
        printk(KERN_CRIT "%s: sys_put_data() error: invalid input!\n", MODNAME);
        ret = -EINVAL;
        goto exit;
    }

    // locking the mutex to avoid concurrency
    mutex_lock(&(rcu.write_lock));

    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);
    
    // allocation of kernel buffer for the message
    buffer = kmalloc(size, GFP_KERNEL);
    if (!buffer) {
        printk(KERN_CRIT "%s: buffer kmalloc error\n", MODNAME);
        ret = -ENOMEM;
        goto exit;
    }

    // copies the user buffer into the kernel buffer
    strcat(source,"\0");
    size += 1;
    ret = copy_from_user(buffer, source, size);
    len = strlen(buffer);
    if (strlen(buffer) < size)
        size = len;

    // initializes the new block to be inserted
    newBlock = kmalloc(sizeof(struct Block), GFP_KERNEL);
    if (newBlock == NULL) {
        printk(KERN_CRIT "%s: newBlock kmalloc error\n", MODNAME);
        ret = -ENOMEM;
        goto exit;
    }
    newBlock->isValid = true;
    newBlock->nextInvalidBlock = -1;
    memcpy(newBlock->data, buffer, size);

    // searches for an overwritable block
    invalidBlockIndex = getInvalidBlockIndex();
    if (invalidBlockIndex == -1){
        printk(KERN_CRIT "%s: no block available\n", MODNAME);
        ret = -ENOMEM;
        goto exit;
    }
        
    // gets the buffer_head
    bh = sb_bread(superblock, invalidBlockIndex);
    if (!bh) {
        printk(KERN_CRIT "%s: buffer_head read error\n", MODNAME);
        ret = -EIO;
        goto exit;
    }

    // move to a new epoch (concurrency avoided with write_lock)
    oldEpoch = rcu.epoch;
    rcu.epoch = (rcu.epoch+1)%EPOCHS;
    printk("%s: put_data (waiting %lu readers)\n", MODNAME, rcu.readers[oldEpoch]);
    wait_event_interruptible(rcu_wq, rcu.readers[oldEpoch] <= 0);
    printk("%s: put_data wait end\n", MODNAME);

    // updates 'firstInvalidBlock' with the value of 'nextInvalidBlock' of the selected block
    ret = updateSuperblockValidEntry(((struct Block *) bh->b_data)->nextInvalidBlock);
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
    newBlock->nextValidBlock = ret;

    // writing the data
    memcpy(bh->b_data, (char *) newBlock, sizeof(struct Block));
    mark_buffer_dirty(bh);

    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) == 0)
        printk("%s: synchronous writing successful", MODNAME);
    else
        printk(KERN_CRIT "%s: synchronous writing failed", MODNAME);
    #endif

    brelse(bh);
    ret = invalidBlockIndex;

    printBlocks();

exit:
    mutex_unlock(&(rcu.write_lock));
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);
    kfree(buffer);
    kfree(newBlock);
    return ret;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char*, destination, size_t, size) {
#else
asmlinkage int sys_get_data(int offset, char* destination, size_t size) {
#endif

    int ret, blocksNumber, myEpoch;
    struct onefilefs_sb_info *superblockInfo;
    struct buffer_head *bh = NULL;
    struct Block *block;

    printk("%s: get_data called ...", MODNAME);

    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);

    // increase the readers counter in rcu
    myEpoch = rcu.epoch;
    __sync_fetch_and_add(&(rcu.readers[myEpoch]),1);

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

    ret = size;

    printk("%s: %d bytes loaded\n", MODNAME, ret);

    brelse(bh);

exit:
    #ifdef TEST_RCU
    ssleep(2);
    #endif
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);
    __sync_fetch_and_sub(&(rcu.readers[myEpoch]),1);
    wake_up_interruptible(&rcu_wq);

    return ret;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset) {
#else
asmlinkage int sys_invalidate_data(int offset) {
#endif

    int ret = 0, blocksNumber, oldEpoch;
    struct onefilefs_sb_info *superblockInfo;
    struct buffer_head *bh;
    struct Block *block;

    printBlocks();

    printk("%s: invalidate_data called ...\n", MODNAME);

    // locking the mutex to avoid concurrency
    mutex_lock(&(rcu.write_lock));

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
    if (offset < 0 || offset >= blocksNumber-2){
        printk(KERN_CRIT "%s: sys_invalidate_data() error: invalid input!\n", MODNAME);
        ret = -EINVAL;
        goto exit;
    }

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

    // check if the block is already invalid
    block = (struct Block *) bh->b_data;
    if (!block->isValid){
        printk(KERN_CRIT "%s: block %d is already invalid\n", MODNAME, offset+2);
        ret = -ENODATA;
        goto exit;
    }

    // move to a new epoch (concurrency avoided with write_lock)
    oldEpoch = rcu.epoch;
    rcu.epoch = (rcu.epoch+1)%EPOCHS;
    printk("%s: invalidate_data (waiting %lu readers)\n", MODNAME, rcu.readers[oldEpoch]);
    wait_event_interruptible(rcu_wq, rcu.readers[oldEpoch] <= 0);
    printk("%s: invalidate_data wait end\n", MODNAME);

    // updates the superblock metadata by placing the newly invalidated block as the first of the invalidated blocks
    ret = updateSuperblockInvalidEntry(offset+2, block->nextValidBlock);
    if (ret < -2) {
        printk(KERN_CRIT "%s: error updating superblock\n", MODNAME);
        ret = -ENODEV;
        goto exit;
    }
    block->isValid = false;
    block->nextValidBlock = -1;
    block->nextInvalidBlock = ret;
    mark_buffer_dirty(bh);

    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) == 0)
        printk("%s: synchronous writing successful", MODNAME);
    else
        printk(KERN_CRIT "%s: synchronous writing failed", MODNAME);
    #endif

    brelse(bh);
    
    printk("%s: block %d has been invalidated\n", MODNAME, offset+2);

    ret = offset+2;

    printBlocks();

exit:
    mutex_unlock(&(rcu.write_lock));
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);
    return ret;
}



#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)       
unsigned long sys_put_data = (unsigned long) __x64_sys_put_data;
unsigned long sys_get_data = (unsigned long) __x64_sys_get_data;
unsigned long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif