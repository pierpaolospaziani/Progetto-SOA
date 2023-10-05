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

static struct super_operations singlefilefs_super_ops = {
};


static struct dentry_operations singlefilefs_dentry_ops = {
};

// filesystem and block device metadata setup
struct block_device_metadata bd_metadata = {0, NULL};
struct filesystem_metadata fs_metadata = {false, 0, " "};


int singlefilefs_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct onefilefs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;

    // Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if(!sb){
	   return -EIO;
    }
    sb_disk = (struct onefilefs_sb_info *)bh->b_data;
    magic = sb_disk->magic;
    brelse(bh);

    // check on the expected magic number
    if(magic != sb->s_magic){
	   return -EBADF;
    }

    sb->s_fs_info = NULL;                                           // FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &singlefilefs_super_ops;                             // set our own operations


    root_inode = iget_locked(sb, 0);                                // get a root inode indexed with 0 from cache
    if (!root_inode){ 
        return -ENOMEM;
    }

    root_inode->i_ino = SINGLEFILEFS_ROOT_INODE_NUMBER;             // this is actually 10
    inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);     // set the root user as owned of the FS root
    root_inode->i_sb = sb;
    root_inode->i_op = &onefilefs_inode_ops;                        // set our inode operations
    root_inode->i_fop = &onefilefs_dir_operations;                  // set our file operations
    
    // update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    // baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;

    sb->s_root->d_op = &singlefilefs_dentry_ops;                    //set our dentry operations

    // unlock the inode to make it usable
    unlock_new_inode(root_inode);

    return 0;
}


static void singlefilefs_kill_superblock(struct super_block *s) {
    
    bool isMounted;

    if (&(fs_metadata.currentlyInUse) != 0) {
        printk(KERN_CRIT "%s: Unable to unmount the filesystem: some thread is using it!\n", MOD_NAME);
        return;
    }

    isMounted = __sync_val_compare_and_swap(&(fs_metadata.isMounted), true, false);
    if (!isMounted) {
        printk(KERN_CRIT "%s: The file system was already unmounted!\n", MOD_NAME);
        return;
    }

    // MANCA LA PERSISTENZA

    // MANCA LA PARTE RCU

    kill_block_super(s);
    printk("%s: Filesystem unmounted succesfully\n",MOD_NAME);
    return;
}


struct dentry *singlefilefs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    bool alreadyMounted;
    int len;
    struct dentry *ret;
    
    // controllo se il filesystem è già montato
    alreadyMounted = __sync_val_compare_and_swap(&(fs_metadata.isMounted), false, true);
    if (alreadyMounted) {
        printk("%s: Filesystem already mounted\n", MODNAME);
        return ERR_PTR(-EBUSY);
    }

    ret = mount_bdev(fs_type, flags, dev_name, data, singlefilefs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk(KERN_CRIT "%s: Error mounting filesystem!",MOD_NAME);
    else{
        // ottieni il nome del loop device in modo tale da accedere i dati successivamente
        len = strlen(dev_name);
        strncpy(fs_metadata.deviceName, dev_name, len);
        fs_metadata.deviceName[len] = '\0';
        bd_metadata.bdev = blkdev_get_by_path(fs_metadata.deviceName, FMODE_READ|FMODE_WRITE, NULL);
        if (bd_metadata.bdev == NULL) {
            printk(KERN_CRIT "%s: impossibile recuperare la struct block_device associata a %s", MODNAME, fs_metadata.deviceName);
            return ERR_PTR(-EINVAL);
        }

        // restore_blocks(); // ripristino dello stato delle strutture del kernel dal dispositivo
        
        printk("%s: Filesystem succesfully mounted on from device %s\n",MOD_NAME,dev_name);
    }

    // MANCA LA PERSISTENZA

    return ret;
}

//file system structure
static struct file_system_type onefilefs_type = {
	.owner   = THIS_MODULE,
    .name    = "singlefilefs",
    .mount   = singlefilefs_mount,
    .kill_sb = singlefilefs_kill_superblock,
};