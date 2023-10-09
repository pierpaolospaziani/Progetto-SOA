#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "filesystem/singlefilefs.h"
#include "defines.h"

int isValidBlock(int);
int getInvalidBlockIndex(void);
int updateSuperblockInvalidEntry(int,int);
int updateValidChain(int,int,int);
int updateSuperblockValidEntry(int);

void printBlocks(void);

void printBlocks() {

    struct buffer_head *bh;
    struct onefilefs_sb_info *superblockInfo;
    int i,blocksNumber,sb_invalid,sb_valid;
    struct Block *block;

    bh = sb_bread(superblock, 0);
    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;
    blocksNumber = superblockInfo->blocksNumber;
    sb_invalid = superblockInfo->firstInvalidBlock;
    sb_valid = superblockInfo->firstValidBlock;
    brelse(bh);

    printk(KERN_ALERT "%s: ====================================================\n", MODNAME);
    printk("%s: SUPER_B:       sb_invalid = %d -       sb_valid = %d\n", MODNAME,sb_invalid,sb_valid);
    for (i=2;i<blocksNumber;i++){
        bh = sb_bread(superblock, i);
        block = (struct Block *)bh->b_data;
        printk("%s: BLOCK %d: nextInvalidBlock = %d - nextValidBlock = %d\n", MODNAME,i,block->nextInvalidBlock,block->nextValidBlock);
    }
    printk(KERN_ALERT "%s: ====================================================\n", MODNAME);
}

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

// update the value of 'firstInvalidBlock' in the superblock with the value passed as a parameter and the valid chain
// returns the old 'firstInvalidBlock'
int updateSuperblockInvalidEntry(int newInvalidBlock, int nextValidBlock) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *superblockInfo;
    int oldInvalidBlock, ret;

    bh = sb_bread(superblock, 0);
    if (!bh)
        return -2;  // -1 can't be used because it indicates that the block is the first in the list of invalid blocks

    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;
    oldInvalidBlock = superblockInfo->firstInvalidBlock;
    
    if (superblockInfo->firstValidBlock == newInvalidBlock){
        superblockInfo->firstValidBlock = nextValidBlock;
    } else{
        ret = updateValidChain(superblockInfo->firstValidBlock, newInvalidBlock, nextValidBlock);
        if (ret < 0)
            return -2;  // -1 can't be used because it indicates that the block is the first in the list of invalid blocks
    }

    superblockInfo->firstInvalidBlock = newInvalidBlock;


    mark_buffer_dirty(bh);

    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) != 0)
        return -2;
    #endif

    brelse(bh);

    return oldInvalidBlock;
}

// update the 'nextValidBlock' value for the previous block in the valid chain
int updateValidChain(int firstValidBlock, int newInvalidBlock, int nextValidBlock) {
    
    struct buffer_head *bh;
    int blockNumber;
    struct Block *block;

    blockNumber = firstValidBlock;
    while (blockNumber != -1){
        bh = sb_bread(superblock, blockNumber);
        if (!bh) {
            return -1;
        }
        block = (struct Block *)bh->b_data;
        if (block->nextValidBlock == newInvalidBlock)
            break;
        else
            blockNumber = block->nextValidBlock;
    }
    block->nextValidBlock = nextValidBlock;

    mark_buffer_dirty(bh);

    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) != 0)
        return -1;
    #endif

    return 0;
}

// update the value of 'firstInvalidBlock' in the superblock with the value passed as a parameter
// returns the old 'firstInvalidBlock'
int updateSuperblockValidEntry(int nextInvalidBlock) {

    struct buffer_head *bh;
    struct onefilefs_sb_info *superblockInfo;
    int oldInvalidBlock, oldValidBlock;

    bh = sb_bread(superblock, 0);
    if (!bh) {
        return -2;  // -1 can't be used because it indicates that the block is the first in the list of invalid blocks
    }

    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;

    oldInvalidBlock = superblockInfo->firstInvalidBlock;
    oldValidBlock = superblockInfo->firstValidBlock;

    superblockInfo->firstInvalidBlock = nextInvalidBlock;
    superblockInfo->firstValidBlock = oldInvalidBlock;

    mark_buffer_dirty(bh);

    #ifdef SYNCHRONUS_WRITE_BACK
    if(sync_dirty_buffer(bh) != 0)
        return -2;
    #endif

    brelse(bh);

    return oldValidBlock;
}
