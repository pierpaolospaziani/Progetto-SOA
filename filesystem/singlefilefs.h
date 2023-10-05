#ifndef _ONEFILEFS_H
#define _ONEFILEFS_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/version.h>

#define MOD_NAME "SINGLE_FILE_FS"

#define MAGIC 0x42424242
#define SB_BLOCK_NUMBER 0
#define DEFAULT_FILE_INODE_BLOCK 1

#define FILENAME_MAXLEN 255

#define SINGLEFILEFS_ROOT_INODE_NUMBER 10
#define SINGLEFILEFS_FILE_INODE_NUMBER 1

#define SINGLEFILEFS_INODES_BLOCK_NUMBER 1

#define UNIQUE_FILE_NAME "the-file"

#define DEFAULT_BLOCK_SIZE 4096
#define METADATA_SIZE 9


//inode definition
struct onefilefs_inode {
	mode_t mode;//not exploited
	uint64_t inode_no;
	uint64_t data_block_number;//not exploited

	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
};

//dir definition (how the dir datablock is organized)
struct onefilefs_dir_record {
	char filename[FILENAME_MAXLEN];
	uint64_t inode_no;
};


//superblock definition
struct onefilefs_sb_info {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	uint64_t inodes_count;//not exploited
	uint64_t free_blocks;//not exploited
	
	// invalid block are managed in a LIFO linked list:
	//  - 'firstInvalidBlock' is the index of the last invalidated block
	//  - each invalid block has in 'nextInvalidBlock' the index of the previous invalid one
	uint64_t firstInvalidBlock;
	uint64_t blocksNumber;

	//padding to fit into a single block
	char padding[ (4 * 1024) - (7 * sizeof(uint64_t))];
};


// block metadata definition
struct block_device_metadata {
    unsigned int usage;
    struct block_device *bdev;
};

// filesystem metadata definition
struct filesystem_metadata {
    bool isMounted;
    unsigned int currentlyInUse;
    char deviceName[20];
};

// block definition
struct Block {
    // metadata
    bool isValid;
    unsigned int nextInvalidBlock;
    // data
    char data[DEFAULT_BLOCK_SIZE - METADATA_SIZE];
};

extern struct block_device_metadata bd_metadata;


// file.c
extern const struct inode_operations onefilefs_inode_ops;
extern const struct file_operations onefilefs_file_operations; 

// dir.c
extern const struct file_operations onefilefs_dir_operations;

#endif
