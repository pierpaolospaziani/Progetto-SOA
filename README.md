# SOA Project: Block-level data management service
## Author
* **Pierpaolo Spaziani** (serial number: 0316331)

## Index
1. [Introduction](#introduction)
2. [Data Structures](#data-structures)
3. [System calls](#system-calls)
4. [File operations](#file-operations)
5. [Guide](#guide)

## Introduction

The project is related to a Linux device driver implementing block-level maintenance of user data, in particular of user messages.
Each block of the block-device has size 4KB and its layout is organized as follows:
- The lower half (X bytes) keeps user data.
- The upper half (4KB-X bytes) keeps metadata for the management of the device.

The device driver is essentially based on system-calls partially supported by the VFS and partially not. The system calls implemented are:

-  `int put_data(char* source, size_t size)`: used to put into one free block of the block-device `size` bytes of the user-space data identified by the `source` pointer. The system call returns an integer representing the offset of the device (the block index) where data have been put. If there is currently no room available on the device, the service returns the *ENOMEM* error;
    
-   `int get_data(int offset, char* destination, size_t size)`: used to read up to `size` bytes from the block at a given `offset`, if it currently keeps data. This system call returns the amount of bytes actually loaded into the `destination` area or zero if no data is currently kept by the device block. This service returns the *ENODATA* error if no data is currently valid and associated with the `offset` parameter.
    
-   `int invalidate_data(int offset)`: used to invalidate data in a block at a given `offset`. This service returns the *ENODATA* error if no data is currently valid and associated with the offset parameter.

The device driver supports file system operations allowing the access to the currently saved data:
-   `open`: for opening the device as a simple stream of bytes.
-   `release`: for closing the file associated with the device.
-   `read`: to access the device file content, according to the order of the delivery of data.

The device driver can support a single mount at a time. When the device is not mounted, both file system operations and the VFS non-supported system calls will return the *ENODEV* error.

The maximum number of manageable blocks is configurable at compile time via the `NBLOCKS` parameter in the *Makefile*.

Writes to the device can be performed by the *page-cache write back daemon* or synchronously, the choice is configurable at compile time via the `SYNCHRONUS_WRITE_BACK` in *defines.h*. If commented, write operations will be executed by the *page-cache write back daemon*, otherwise synchronously.

## Data Structures

### Blocks
Blocks are composed of **12 bytes of metadata** and **4084 bytes of data**:
 ```
struct Block {
    bool isValid;
    unsigned int nextInvalidBlock;
    unsigned int nextValidBlock;
    char data[DEFAULT_BLOCK_SIZE - METADATA_SIZE];
};
  ```
-   `bool isValid`: indicates the validity of the block.
-   `unsigned int nextInvalidBlock`: the offset of the next *invalid* block.
-   `unsigned int nextValidBlock`: the offset of the next *valid* block.
-   `char* data`: data buffer.
> **Note:** Even if the metadata occupies *9 bytes*, it is considered *12 bytes* for alignment reasons.

### Superblock
The device superblock is composed of the following fields:
 ```
struct onefilefs_sb_info {
  uint64_t version;
  uint64_t magic;
  uint64_t block_size;
  uint64_t firstInvalidBlock;
  uint64_t firstValidBlock;
  uint64_t blocksNumber;
};
  ```
-   `uint64_t version`: indicates the file system version.
-   `uint64_t magic`: indicates the magic number associated with the file system.
-   `uint64_t block_size`: indicates the size of each block of memory that makes up the device.
-   `uint64_t firstInvalidBlock`: indicates the offset of the last invalidated block.
-   `uint64_t firstValidBlock`: indicates the offset of the last validated block.
-   `uint64_t blocksNumber`: indicates the total number of blocks.
> **Note:** *valid* and *invalid* blocks are managed in two LIFO linked list to optimize reads and writes.

### File system metadata
A structure maintained in RAM holds file system metadata:
  ```
  struct filesystem_metadata {
    bool isMounted;
    unsigned int currentlyInUse;
};
  ```
-   `bool isMounted`: indicates whether the file system is mounted.
-   `unsigned int currentlyInUse`: number of threads that are currently using the file system.

### RCU metadata
The RCU approach was used for synchronization. The metadata supporting the system are:
  ```
  struct rcu_metadata {
    unsigned long readers[EPOCHS];
    unsigned long epoch;
    struct mutex write_lock;
};

wait_queue_head_t rcu_wq;
  ```
-   `unsigned long readers[EPOCHS]`: each position refers to an epoch and each value indicates the number of readers in that specific epoch.
-   `unsigned long epoch`: indicates the current epoch.
- `struct mutex write_lock`: used to avoid concurrent writes.
- `wait_queue_head_t rcu_wq`: used to make writers wait when readers are present.
> **Note:** *EPOCHS* is a configurable macro in *rcu.h*, the default value is 2.

## System calls
### int put_data(char* source, size_t size)
1. Checks the validity of the input parameters.
2. Takes the `write_lock` lock to avoid concurrent writes.
3. Increments atomically with `__sync_fetch_and_add` the *usage counter* of the file system's `currentlyInUse` field.
4. `kmalloc` allocates a kernel buffer to accommodate the user message, copied with `copy_from_user`.
5. Initializes and populates the block that must replace the first invalid (if it exists), i.e. the one at offset `firstInvalidBlock` (value maintained in the *superblock*). If it does not exist, the system call ends with the *ENOMEM* error.
6. Starts a new RCU epoch and, if there are readers in the previous one, waits for the end of the readings by going to sleep on `rcu_wq` with `wait_event_interruptible`.
7. Overwrites the *selected block* and updates the linked lists of *valid* and *invalid* blocks:
  - in the *superblock*:
    - `firstInvalidBlock` updated with the value of `nextInvalidBlock` of the *selected block*.
    - `firstValidBlock` updated with the offset of the *selected block* (i.e. the old value of `firstInvalidBlock`).
  - in the *selected block*:
    - `nextInvalidBlock` invalidated (set to -1).
    - `nextValidBlock` updated with the old *superblock* `firstValidBlock` value.
  ```mermaid
  graph LR
  A(Superblock) -- firstInvalidBlock --> B(Selected Block)
  A -- firstValidBlock --> C(Valid Block)
  B -- nextInvalidBlock --> D(Invalid Block)
  ```
  ```mermaid
  graph LR
  A(Superblock) -- firstInvalidBlock --> D(Invalid Block)
  A -- firstValidBlock --> B(Selected Block)
  B -- nextValidBlock --> C(Valid Block)
  ```
8. Release the `write_lock` lock.
9. Decrements atomically with `__sync_fetch_and_sub` the *usage counter* of the `currentlyInUse` file system.

### int get_data(int offset, char* destination, size_t size)
1. Checks the validity of the input parameters.
2. Increments atomically with `__sync_fetch_and_add` the *usage counter* of the `currentlyInUse` file system.
3. Increments atomically with `__sync_fetch_and_add` the *readers counter* in the RCU metadata for the current epoch in `readers[epoch]`.
4. Access the requested block (with `offset+2` to avoid superblock and inode).
5. Checks the validity of the block from the `isValid` field, if it is invalid the system call ends with the *ENODATA* error.
6. Using `copy_to_user`, copies `size` bytes of the block's `data` field into the `destination` buffer.
7. Decrements atomically with `__sync_fetch_and_sub` the _usage counter_ of the `currentlyInUse` file system.
8. Decrements atomically with `__sync_fetch_and_sub` the *readers counter* in the RCU metadata for the current epoch in `readers[epoch]`.
9. Wakes up sleeping threads on `rcu_wq` with `wake_up_interruptible`.

### int invalidate_data(int offset)
1. Checks the validity of the input parameter.
2. Takes the `write_lock` lock to avoid concurrent writes.
3. Increments atomically with `__sync_fetch_and_add` the *usage counter* of the `currentlyInUse` file system.
4. Accesses the requested block (with `offset+2` to avoid superblock and inode).
5. Checks the validity of the block from the `isValid` field, if it is invalid the system call ends with the *ENODATA* error.
6. Starts a new RCU epoch and, if there are readers in the previous one, waits for the end of the readings by going to sleep on `rcu_wq` with `wait_event_interruptible`.
7. Updates the *selected block* and updates the liked lists of *valid* and *invalid* blocks:
  - in the *superblock*:
    - `firstInvalidBlock` updated with the offset of the *selected block*.
  - in the block preceding the one selected in the valid linked list, the one with `nextValidBlock` equal to the offset of the *selected block* (*Valid Block 1* in the  scheme):
    - `nextValidBlock` updated with the `nextValidBlock` value of the *selected block*.
  - in the *selected block*:
    - `isValid` set to *False*.
    - `nextInvalidBlock` updated with the old *superblock* `firstInvalidBlock` value.
    - `nextValidBlock` invalidated (set to -1).
  ```mermaid
  graph LR
  A(Superblock) -- firstInvalidBlock --> D(Invalid Block)
  A -- firstValidBlock --> B(Valid Block 1)
  B -- nextValidBlock --> C(Selected Block)
  C -- nextValidBlock --> E(Valid Block 2)
  ```
  ```mermaid
  graph LR
  A(Superblock) -- firstInvalidBlock --> B(Selected Block)
  A -- firstValidBlock --> C(Valid Block 1)
  B -- nextInvalidBlock --> D(Invalid Block)
  C -- nextValidBlock --> E(Valid Block 2)
  ```
8. Release the `write_lock` lock.
9. Decrements atomically with `__sync_fetch_and_sub` the *usage counter* of the `currentlyInUse` file system.

## File operations

### int onefilefs_open(struct inode *inode, struct file *file)
1. Increments atomically with `__sync_fetch_and_add` the *usage counter* of the `currentlyInUse` file system.
2. Checks with the `isMounted` field whether the device is mounted.
3. Opens the file and checks if it was opened in *read-only* mode.

### int onefilefs_release(struct inode *inode, struct file *file)
1. Checks with the `isMounted` field whether the device is mounted.
2. Decrements atomically with `__sync_fetch_and_sub` the *usage counter* of the `currentlyInUse` file system.
3. The file is closed.

### ssize_t onefilefs_read(struct file * filp, char __user * buf, size_t len, loff_t * off)
1. Checks with the `isMounted` field whether the device is mounted.
2. Increments atomically with `__sync_fetch_and_add` the *readers counter* in the RCU metadata for the current epoch in `readers[epoch]`.
3. Scrolling the *valid linked list*, copies the `data` fields from the blocks and then transfers them with `copy_to_user` into `buf`.
4. Decrements atomically with `__sync_fetch_and_sub` the _usage counter_ of the `currentlyInUse` file system.
5. Decrements atomically with `__sync_fetch_and_sub` the *readers counter* in the RCU metadata for the current epoch in `readers[epoch]`.
6. Wakes up sleeping threads on `rcu_wq` with `wake_up_interruptible`.

## Guide

### Parameters configuration
- In *Makefile*:
    - `NBLOCKS`: allows you to choose the number of blocks (includes superblock and inode).
    - `NTHREADS`: allows you to choose the number of threads generated in the automatic testing software `test`.
- In *defines.h*:
    - `SYNCHRONUS_WRITE_BACK`: if commented, write operations will be executed by the page-cache write back daemon, otherwise synchronously.

### Compile & Install
From the main directory:
1. `make` compiles modules and user programs.
2. `make insmod` installs the modules, registers the system calls, the device driver and the file system.
3. `make create-fs` creates the file image.
4. `make mount-fs` mounts the file system.

### Test
After executing all the *Compile & Install* steps, from the `test` directory:
- `./user` starts the user interactive testing software.
- `./test` starts the automatic testing software. This generates a set of threads that concurrently invoke system calls and the *cat* command (to test *file operations*).

The results of the operations can be observed on *standard output* and in `dmesg`.

### Clean up
From the main directory:
1. `make unmount-fs` unmounts the file system.
2. `make rmmod` removes modules.
3. `make clean` removes files generated by compilation.