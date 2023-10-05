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
    struct block_device *bdev_temp;

    printk("%s: sys_put_data() invocata\n", MODNAME);

    // sanity check
    if (source == NULL || size >= DEFAULT_BLOCK_SIZE - METADATA_SIZE) return -EINVAL;
    
    // allocazione di memoria dinamica per contenere il messaggio utente
    buffer = kmalloc(size+1, GFP_KERNEL);
    if (!buffer) {
        printk(KERN_CRIT "%s: errore kmalloc, impossibilità di allocare memoria per la ricezione del buffer utente\n", MODNAME);
        return -ENOMEM;
    }

    // copia size bytes dal buffer utente al buffer kernel
    ret = copy_from_user(buffer, source, size); // ret è il numero di byte NON copiati (su un massimo di size).
    len = strlen(buffer);
    if (strlen(buffer) < size)
        size = len;
    buffer[size] = end_str;
    printk("%s: il messaggio da inserire è: %s (len=%lu)\n", MODNAME, buffer, size);

    // segnala la presenza del reader sulla variabile bdev (for the unmount check)
    __sync_fetch_and_add(&(bd_metadata.usage),1);
    bdev_temp = bd_metadata.bdev;
    if (bdev_temp == NULL) {
        printk(KERN_CRIT "%s: nessun device montato", MODNAME);
        ret = -ENODEV;
        goto put_exit;
    }

    // popola la struct block_device_layout da scrivere poi in buffer_head
    newBlock = kmalloc(sizeof(struct Block), GFP_KERNEL);
    if (newBlock == NULL) {
        printk(KERN_CRIT "%s: errore kmalloc, impossibilità di allocare memoria per newBlock\n", MODNAME);
        // wake_up_interruptible(&unmount_wq);
        kfree(buffer);
        return -ENOMEM;
    }
    newBlock->isValid = true;
    newBlock->nextInvalidBlock = -1;
    memcpy(newBlock->data, buffer, size+1); // +1 per il terminatore di stringa

    // cerca un blocco sovrascrivibile
    invalidBlockIndex = getInvalidBlockIndex();
    if (invalidBlockIndex == -1){
        printk(KERN_CRIT "%s: nessun blocco disponibile per inserire il messaggio\n", MODNAME);
        ret = -ENOMEM;
        goto put_exit;
    }

    //attesa della fine del grace period
    // synchronize_srcu(&(au_info.srcu));

    // prendi il lock in scrittura per la concorrenza con invalidate_data
    // mutex_lock(&(rcu.write_lock));
        
    // dati in cache
    bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, invalidBlockIndex);
    if (!bh) {
        printk(KERN_CRIT "%s: impossibile leggere il buffer_head, invalidBlockIndex = %d\n", MODNAME,invalidBlockIndex);
        // sel_blk = set_invalid(0);
        ret = -EIO;
        // mutex_unlock(&(rcu.write_lock));    // <---
        goto put_exit;
    }

    // aggiorna 'firstInvalidBlock' con il valore di 'nextInvalidBlock' del blocco selezionato
    ret = updateSuperblockInvalidEntry(((struct Block *) bh->b_data)->nextInvalidBlock);
    if (ret < -1) {
        printk(KERN_CRIT "%s: problemi con l'aggiornamento del superblocco: %d\n", MODNAME, ret);
        ret = -ENODEV;
        goto put_exit;
    }

    if (bh->b_data != NULL) {
        memcpy(bh->b_data, (char *) newBlock, sizeof(struct Block));
        mark_buffer_dirty(bh);
    }

    // forza la scrittura in modo sincrono sul device
    // se non si vuole utilizzare il page-cache write back daemon, la scrittura del blocco viene riportata nel device in maniera sincrona.
// #ifdef SYNC_WRITE_BACK 
//     if(sync_dirty_buffer(bh) == 0) {
//         printk("%s: scrittura sincrona avvenuta con successo", MODNAME);
//     } else
//         printk(KERN_CRIT "%s: scrittura sincrona fallita", MODNAME);
// #endif

    brelse(bh);
    ret = invalidBlockIndex;

    // aggiunta del blocco appena scritto alla rcu_list per renderlo visibile
    // list_insert(&head, i);    // <---
    // printk("%s: blocco %d aggiunto alla lista dei messaggi validi\n", MODNAME, i);

    // mutex_unlock(&(rcu.write_lock));

put_exit:
    __sync_fetch_and_sub(&(bd_metadata.usage),1);
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

    int ret, len, return_val, blocksNumber;
    char end_str = '\0';
    // unsigned long my_epoch;
    struct onefilefs_sb_info *superblock;
    struct buffer_head *bh = NULL;
    struct block_device *bdev_temp;
    struct Block *block;

    printk("%s: get_data invocata", MODNAME);

    bh = sb_bread(bd_metadata.bdev->bd_super, 0);
    if(!bh){
        return -EIO;
    }
    superblock = (struct onefilefs_sb_info *)bh->b_data;
    blocksNumber = superblock->blocksNumber;
    brelse(bh);

    if (offset < 0 || offset >= blocksNumber-2 || size < 0 || destination == NULL){
        printk(KERN_CRIT "%s: sys_get_data() error: invalid input!\n", MODNAME);
        return -EINVAL;
    }

    size = (size >= DEFAULT_BLOCK_SIZE - METADATA_SIZE) ? (DEFAULT_BLOCK_SIZE - METADATA_SIZE) : size;
    
    // segnala la presenza del reader sulla variabile bdev
    __sync_fetch_and_add(&(bd_metadata.usage),1);
    bdev_temp = bd_metadata.bdev;
    if (bdev_temp == NULL) {
        printk(KERN_CRIT "%s: No device mounted", MODNAME);
        __sync_fetch_and_sub(&(bd_metadata.usage),1);
        // wake_up_interruptible(&unmount_wq);
        return -ENODEV;
    }

    // segnala la presenza del reader per evitare che uno scrittore riutilizzi lo stesso blocco mentre lo si sta leggendo
    // my_epoch = __sync_fetch_and_add(&(rcu.epoch),1);

    // dati in cache
    bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, offset+2);
    if (!bh) {
        return_val = -EIO;
        goto get_exit;
    }

    if (bh->b_data != NULL) {
        printk("%s: [blocco %d]\n", MODNAME, offset+2);
        block = (struct Block *) bh->b_data;
        if (!block->isValid){
            printk(KERN_CRIT "%s: il blocco %d richiesto non è valido\n", MODNAME, offset);
            return_val = -ENODATA;
            goto get_exit;
        }

        len = strlen(block->data);
        if (size > len) { // richiesta una size maggiore del contenuto effettivo del blocco dati
            size = len; 
            ret = copy_to_user(destination, block->data, size);
            return_val = size - ret;
            ret = copy_to_user(destination+return_val, &end_str, 1);
        } 
        else { // richiesta una size minore o uguale del contenuto effettivo del blocco dati
            ret = copy_to_user(destination, block->data, size);
            return_val = size - ret;
            ret = copy_to_user(destination+return_val, &end_str, 1);
        }
        if (strlen(block->data) < size) return_val = strlen(block->data);
        printk("%s: bytes caricati nell'area di destinazione %d\n", MODNAME, return_val);
    }
    else return_val = 0;

    brelse(bh);

get_exit:
    // // the first bit in my_epoch is the index where we must release the counter
    // index = (my_epoch & MASK) ? 1 : 0;
    // __sync_fetch_and_add(&(rcu.standing[index]),1);
    __sync_fetch_and_sub(&(bd_metadata.usage),1);
    // wake_up_interruptible(&readers_wq);
    // wake_up_interruptible(&unmount_wq);

    return return_val; // the amount of bytes actually loaded into the destination area

    return 0;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset) {
#else
asmlinkage int sys_invalidate_data(int offset) {
#endif

    int ret = 0, blocksNumber;
    
    struct onefilefs_sb_info *superblock;
    struct buffer_head *bh;
    struct Block *block;
    struct block_device *bdev_temp;

    printk("%s: invalidate_data invocata\n", MODNAME);

    bh = sb_bread(bd_metadata.bdev->bd_super, 0);
    if(!bh){
        return -EIO;
    }
    superblock = (struct onefilefs_sb_info *)bh->b_data;
    blocksNumber = superblock->blocksNumber;
    brelse(bh);

    if (offset < 0 || offset >= blocksNumber-2) return -EINVAL;

    // segnala la presenza del reader sulla variabile bdev
    __sync_fetch_and_add(&(bd_metadata.usage),1);
    bdev_temp = bd_metadata.bdev;
    if (bdev_temp == NULL) {
        printk(KERN_CRIT "%s: nessun device montato", MODNAME);
        __sync_fetch_and_sub(&(bd_metadata.usage),1);
        // wake_up_interruptible(&unmount_wq);
        return -ENODEV;
    }

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

    // aggiorna i metadati del blocco
    bh = (struct buffer_head *) sb_bread(bdev_temp->bd_super, offset+2);
    if (!bh) {
        ret = -EIO;
        goto inv_exit;
    }

    if (bh->b_data != NULL) {
        block = (struct Block *) bh->b_data;
        if (!block->isValid){
            printk(KERN_CRIT "%s: il blocco %d è già invalidato\n", MODNAME, offset);
            ret = -ENODATA;
            goto inv_exit;
        }
        ret = updateSuperblockInvalidEntry(offset+2);
        if (ret < -1) {
            printk(KERN_CRIT "%s: problemi con l'aggiornamento del superblocco\n", MODNAME);
            ret = -ENODEV;
            goto inv_exit;
        }
        block->isValid = false;
        block->nextInvalidBlock = ret;
        mark_buffer_dirty(bh);
    }

    // forza la scrittura in modo sincrono sul device
// #ifdef SYNC_WRITE_BACK 
//     if(sync_dirty_buffer(bh) == 0) {
//         printk("%s: scrittura sincrona avvenuta con successo", MODNAME);
//     } else
//         printk(KERN_CRIT "%s: scrittura sincrona fallita", MODNAME);
// #endif

    brelse(bh);
    
    printk("%s: block %d has been invalidated\n", MODNAME, offset);

    ret = offset+2;

inv_exit:
    // mutex_unlock(&(rcu.write_lock));
    __sync_fetch_and_sub(&(bd_metadata.usage),1);
    // wake_up_interruptible(&unmount_wq);
    return ret;
}



#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)       
unsigned long sys_put_data = (unsigned long) __x64_sys_put_data;
unsigned long sys_get_data = (unsigned long) __x64_sys_get_data;
unsigned long sys_invalidate_data = (unsigned long) __x64_sys_invalidate_data;
#else
#endif