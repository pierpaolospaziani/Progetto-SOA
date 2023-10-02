#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "singlefilefs.h"
#include "../common_header.h"

/*
	This makefs will write the following information onto the disk
	- BLOCK 0, superblock;
	- BLOCK 1, inode of the unique file (the inode for root is volatile);
	- BLOCK 2, ..., datablocks of the unique file 
*/

int main(int argc, char *argv[])
{
	int fd, nbytes, blocksNumber, blockIndex, bytesLeft, i;
    unsigned int indexNextBlock;
	ssize_t ret;
	struct onefilefs_sb_info sb;
	struct onefilefs_inode root_inode;
	struct onefilefs_inode file_inode;
	struct onefilefs_dir_record record;
	char *block_padding;
	struct Block *dummyBlock;
	unsigned char *metadata;

	if (argc != 3) {
        printf("Usage: mkfs-singlefilefs <device> <num_blocks>\n");
        return -1;
    }

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

	blocksNumber = strtol(argv[2], NULL, 10);

	//pack the superblock
	sb.version = 1;
	sb.magic = MAGIC;
	sb.firstInvalidBlock = 2;					// <-- DA VEDERE !!!
	sb.block_size = DEFAULT_BLOCK_SIZE;

	ret = write(fd, (char *)&sb, sizeof(sb));
	if (ret != DEFAULT_BLOCK_SIZE) {
		printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
		close(fd);
		return ret;
	}
	printf("Super block written succesfully\n");

	// write file inode
	file_inode.mode = S_IFREG;
	file_inode.inode_no = SINGLEFILEFS_FILE_INODE_NUMBER;
	file_inode.file_size = blocksNumber*DEFAULT_BLOCK_SIZE;
	printf("File size is %ld\n",file_inode.file_size);
	fflush(stdout);

	ret = write(fd, (char *)&file_inode, sizeof(file_inode));
	if (ret != sizeof(root_inode)) {
		printf("The file inode was not written properly.\n");
		close(fd);
		return -1;
	}
	printf("File inode written succesfully.\n");
	
	//padding for block 1
	nbytes = DEFAULT_BLOCK_SIZE - sizeof(file_inode);
	block_padding = malloc(nbytes);

	ret = write(fd, block_padding, nbytes);
	if (ret != nbytes) {
		printf("The padding bytes are not written properly. Retry your mkfs\n");
		close(fd);
		return -1;
	}
	printf("Padding in the inode block written sucessfully.\n");

	free(block_padding);

	dummyBlock = malloc(DEFAULT_BLOCK_SIZE);
	if (dummyBlock == NULL) {
	    printf("dummyBlock allocation has failed.\n");
		close(fd);
		return -1;
	}
	dummyBlock->isValid = false;
	memset(dummyBlock->data, 0, DEFAULT_BLOCK_SIZE - METADATA_SIZE);

	//write file datablock
	for (blockIndex = 2; blockIndex < blocksNumber; blockIndex++) {
		
	    if (blockIndex == blocksNumber-1)
			dummyBlock->nextInvalidBlock = -1;
		else
			dummyBlock->nextInvalidBlock = blockIndex+1;

		// scrittura dei blocchi
		ret = write(fd, dummyBlock, sizeof(struct Block));
		if (ret != sizeof(struct Block)) {
			printf("Writing file datablock has failed, just %ld bytes\n", ret);
			fflush(stdout);
			close(fd);
			return -1;
		}
	}
	printf("File datablock has been written succesfully.\n");
	close(fd);
	return 0;
}