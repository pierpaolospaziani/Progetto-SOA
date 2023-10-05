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

// checks if a block inside the device is valid
int isValidBlock(int blockNumber) {

    struct buffer_head *bh;
    struct Block *block;

    bh = sb_bread(superblock, blockNumber);
    if (!bh) {
        return -1;
    }

    block = (struct Block *) bh->b_data;

    brelse(bh);
    return block->isValid;
}

// gets the index of the first invalid block
int getInvalidBlockIndex() {

    struct buffer_head *bh;
    struct onefilefs_sb_info *superblockInfo;

    bh = sb_bread(superblock, 0);
    if (!bh) {
        return -1;
    }

    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;

    brelse(bh);
    return superblockInfo->firstInvalidBlock;
}

// update the value of 'firstInvalidBlock' in the superblock with the value passed as a parameter
// returns the old 'firstInvalidBlock'
int updateSuperblockInvalidEntry(int blockNumber) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *superblockInfo;
    int oldInvalidBlock;

    bh = sb_bread(superblock, 0);
    if (!bh) {
        return -2;  // -1 can't be used because it indicates that the block is the first in the list of invalid blocks
    }

    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;
    oldInvalidBlock = superblockInfo->firstInvalidBlock;
    superblockInfo->firstInvalidBlock = blockNumber;
    mark_buffer_dirty(bh);

    // if 'SYNCHRONUS_WRITE_BACK' is not defined, write operations will be executed by the page-cache write back daemon
    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) != 0)
        return -2;
    #endif

    brelse(bh);

    return oldInvalidBlock;
}
