#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "filesystem/singlefilefs.h"
#include "utils_header.h"

int isValidBlock(int);
int getInvalidBlockIndex(void);
int updateSuperblockInvalidEntry(int);

// controlla se un blocco all'interno del dispositivo è valido
int isValidBlock(int blockNumber) {

    struct buffer_head *bh;
    struct Block *block;

    bh = sb_bread(bd_metadata.bdev->bd_super, blockNumber);
    if (!bh) {
        return -1;
    }

    block = (struct Block *) bh->b_data;

    brelse(bh);
    return block->isValid;
}

// ottiene l'indice del primo blocco invalido
int getInvalidBlockIndex() {

    struct buffer_head *bh;
    struct onefilefs_sb_info *superblock;

    bh = sb_bread(bd_metadata.bdev->bd_super, 0);
    if (!bh) {
        return -1;
    }

    superblock = (struct onefilefs_sb_info *)bh->b_data;

    brelse(bh);
    return superblock->firstInvalidBlock;
}

// aggiorna il valore di 'firstInvalidBlock' nel superblocco con il valore passato come parametro
int updateSuperblockInvalidEntry(int blockNumber) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *superblock;

    bh = sb_bread(bd_metadata.bdev->bd_super, 0);
    if (!bh) {
        return -1;
    }

    superblock = (struct onefilefs_sb_info *)bh->b_data;
    superblock->firstInvalidBlock = blockNumber;
    mark_buffer_dirty(bh);

    //se non si vuole utilizzare il page-cache write back daemon, la scrittura del blocco viene riportata nel device in maniera sincrona.
    // #ifdef SYNC
    // sync_dirty_buffer(bh);
    // #endif

    brelse(bh);
    return 0;
}
