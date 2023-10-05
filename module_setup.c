#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
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
#include <linux/delay.h>
#include <linux/pid.h>
#include <linux/tty.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>

#include "lib/include/scth.h"
#include "utils_header.h"
#include "my_syscalls.c"
#include "filesystem/singlefilefs_src.c"
#include "filesystem/file.c"
#include "filesystem/dir.c"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pierpaolo Spaziani <pierpaolo.spaziani@alumni.uniroma2.eu>");
MODULE_DESCRIPTION("Block-level data management service");

unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);
unsigned long the_ni_syscall;
unsigned long new_sys_call_array[] = {0x0,0x0,0x0};
#define HACKED_ENTRIES (int)(sizeof(new_sys_call_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};

int major;

// Startup function
int init_module(void) {

    int i, ret;

    printk("%s: Module startup ...", MODNAME);
    printk("%s: System Call Table address: %p\n", MODNAME, (void *)the_syscall_table);
    printk("%s: Initializing hacked entries %d\n", MODNAME, HACKED_ENTRIES);

    // +===================+
    // | System call setup |
    // +===================+
    new_sys_call_array[0] = (unsigned long) sys_put_data;
    new_sys_call_array[1] = (unsigned long) sys_get_data;
    new_sys_call_array[2] = (unsigned long) sys_invalidate_data;
    ret = get_entries(restore, HACKED_ENTRIES, (unsigned long *) the_syscall_table, &the_ni_syscall);
    if (ret != HACKED_ENTRIES) {
        printk(KERN_CRIT "%s: Couldn't hack %d entries (just %d)\n", MODNAME, HACKED_ENTRIES, ret);
        return -1;
    }
    unprotect_memory();
    for (i = 0; i < HACKED_ENTRIES; i++) {
        ((unsigned long *) the_syscall_table)[restore[i]] = (unsigned long) new_sys_call_array[i];
    }
    protect_memory();
    printk("%s: New system calls installed\n", MODNAME);


    // +=====================+
    // | Device Driver setup |
    // +=====================+
    major = __register_chrdev(0, 0, 256, DEVICE_NAME, &onefilefs_file_operations);
    if (major < 0) {
        printk(KERN_CRIT "%s: Device driver registration faild!\n", MODNAME);
        return major;
    }
    printk("%s: Device driver registered - Major number: %d\n", MODNAME, major);


    // +==================+
    // | Filesystem setup |
    // +==================+
    ret = register_filesystem(&onefilefs_type);
    if (likely(ret == 0))
        printk("%s: Filesystem registered\n", MODNAME);
    else
        printk(KERN_CRIT "%s: Filesystem registration faild!\n", MODNAME);


    return 0;
}


// Shutdown function
void cleanup_module(void) {

    int i;

    printk("%s: Module cleanup ...\n", MODNAME);

    // +====================+
    // | System call remove |
    // +====================+
    unprotect_memory();
    for (i = 0; i < HACKED_ENTRIES; i++) {
        ((unsigned long *) the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();
    printk("%s: System call table restored\n", MODNAME);

    // +==========================+
    // | Device Driver unregister |
    // +==========================+
    unregister_chrdev(major, DEVICE_NAME);
    printk("%s: Device driver (%d) unregistered\n", MODNAME, major);

    // +=======================+
    // | Filesystem unregister |
    // +=======================+
    if (likely(unregister_filesystem(&onefilefs_type) == 0))
        printk("%s: Filesystem unregistered\n", MODNAME);
    else
        printk(KERN_CRIT "%s: Unable to unregister the filesystem!\n", MODNAME);

    return;
}