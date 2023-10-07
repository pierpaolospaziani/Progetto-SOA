#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "singlefilefs.h"


int onefilefs_open(struct inode *inode, struct file *file) {
    
    printk("%s: trying to open ...\n", MOD_NAME);

    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);
    
    // check if it's mounted
    if (!&(fs_metadata.isMounted)) {
        printk(KERN_CRIT "%s: No device mounted\n", MOD_NAME);
        return -ENODEV;
    }

    // the device must be read_only
    if (file->f_mode & FMODE_WRITE) {
        printk(KERN_CRIT "%s: Unable to open the device in write mode\n", MOD_NAME);
        return -EROFS;
    }
    
    // decrease the usage counter
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);

    printk("%s: ... open success\n", MOD_NAME);
    
    return 0;
}


int onefilefs_release(struct inode *inode, struct file *file) {

    printk("%s: trying to release ...\n", MOD_NAME);
    
    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);

    // check if it's mounted
    if (!&(fs_metadata.isMounted)) {
        printk(KERN_CRIT "%s: No device mounted\n", MOD_NAME);
        return -ENODEV;
    }

    // decrease the usage counter
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);

    printk("%s: ... release success\n", MOD_NAME);

    return 0;
}


ssize_t onefilefs_read(struct file * filp, char __user * buf, size_t len, loff_t * off) {

    int blockIndex, ret, length, offset=0, blocksNumber;
    struct onefilefs_sb_info *superblockInfo;
    struct buffer_head *bh = NULL;
    struct Block *block;
    char *buffer;

    if (*off != 0) return 0;
    
    printk("%s: trying to read ...\n", MOD_NAME);

    // increase the usage counter
    __sync_fetch_and_add(&(fs_metadata.currentlyInUse),1);

    // check if it's mounted
    if (!&(fs_metadata.isMounted)) {
        printk(KERN_CRIT "%s: No device mounted\n", MOD_NAME);
        ret = -ENODEV;
        goto exit;
    }

    bh = sb_bread(superblock, 0);
    if(!bh){
        ret = -EIO;
        goto exit;
    }
    superblockInfo = (struct onefilefs_sb_info *)bh->b_data;
    blocksNumber = superblockInfo->blocksNumber;
    brelse(bh);

    // read only valid blocks
    for (blockIndex = 2; blockIndex < blocksNumber; blockIndex++){
        bh = sb_bread(superblock, blockIndex);
        if (!bh) {
            ret = -EIO;
            goto exit;
        }
        if (bh->b_data != NULL) {
            block = (struct Block *) bh->b_data;
            if (block->isValid){
                length = strlen(block->data);

                buffer = kmalloc(strlen(block->data)+1, GFP_KERNEL);
                if (!buffer) {
                    printk("%s: kmalloc error\n", MOD_NAME);
                    ret = -1;
                    goto exit;
                }

                memcpy(buffer, block->data, length);
                strcat(buffer, "\n");
                length = strlen(buffer);

                ret = copy_to_user(buf+offset, buffer, length);
                if (ret != 0){
                    printk(KERN_CRIT "%s: Unable to copy %d bytes from the block n.%d\n", MOD_NAME, ret, blockIndex);
                    ret = -EIO;
                    goto exit;
                }
                offset += length;
            }
        }
        brelse(bh);
    }
    ret = offset;

    printk("%s: ... read success\n", MOD_NAME);

exit:
    __sync_fetch_and_sub(&(fs_metadata.currentlyInUse),1);
    *off += ret;

    return ret;
}


struct dentry *onefilefs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {

    struct onefilefs_inode *FS_specific_inode;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh = NULL;
    struct inode *the_inode = NULL;

    printk("%s: Running the lookup inode-function for name %s",MOD_NAME,child_dentry->d_name.name);

    if(!strcmp(child_dentry->d_name.name, UNIQUE_FILE_NAME)){

    	// get a locked inode from the cache 
        the_inode = iget_locked(sb, 1);
        if (!the_inode)
       		return ERR_PTR(-ENOMEM);

    	// already cached inode - simply return successfully
    	if(!(the_inode->i_state & I_NEW)){
    		return child_dentry;
    	}

    	// this work is done if the inode was not already cached
        inode_init_owner(&init_user_ns, the_inode, NULL, S_IFREG );
    	the_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;
        the_inode->i_fop = &onefilefs_file_operations;
    	the_inode->i_op = &onefilefs_inode_ops;

    	// just one link for this file
    	set_nlink(the_inode,1);

    	// now we retrieve the file size via the FS specific inode, putting it into the generic inode
        bh = (struct buffer_head *)sb_bread(sb, SINGLEFILEFS_INODES_BLOCK_NUMBER );
        if(!bh){
    		iput(the_inode);
    		return ERR_PTR(-EIO);
        }
    	FS_specific_inode = (struct onefilefs_inode*)bh->b_data;
    	the_inode->i_size = FS_specific_inode->file_size;
        brelse(bh);

        d_add(child_dentry, the_inode);
    	dget(child_dentry);

    	// unlock the inode to make it usable 
        unlock_new_inode(the_inode);

    	return child_dentry;
    }
    return NULL;
}

// look up goes in the inode operations
const struct inode_operations onefilefs_inode_ops = {
    .lookup = onefilefs_lookup,
};

const struct file_operations onefilefs_file_operations = {
    .owner   = THIS_MODULE,
    .open    = onefilefs_open,
    .release = onefilefs_release,
    .read    = onefilefs_read,
};
