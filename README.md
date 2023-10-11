# SOA Project: Block-level data management service
## Author
* **Pierpaolo Spaziani** (serial number: 0316331)

## Index
1. [Introduction](#introduction)
2. [Data Structures](#data-structures)
3. [System call](#system-call)
4. [File operation](#file-operation)
5. [Sincronizzazione](#sincronizzazione)
6. [Software di livello user](#software-di-livello-user)
7. [Howto](#howto)

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

## Data Structures

### Blocks
Blocks are composed of **12 bytes of metadata** and **4084 bytes of data**:
-   `bool isValid`: indicates the validity of the block.
-   `unsigned int nextInvalidBlock`: the offset of the next *invalid* block.
-   `unsigned int nextValidBlock`: the offset of the next *valid* block.
-   `char* data`: data buffer.
> **Note:** Even if the metadata occupies *9 bytes*, it is considered *12 bytes* for alignment reasons.

### Superblock
The device superblock is composed of the following fields:
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
Per la sincronizzazione è stato utilizzato l'approccio RCU. I metadati a supporto del sistema sono:
  ```
  struct rcu_metadata {
    unsigned long readers[EPOCHS];
    unsigned long epoch;
    struct mutex write_lock;
};

wait_queue_head_t rcu_wq;
  ```
-   `unsigned long readers[EPOCHS]`: ogni posizione fa riferimento ad un'epoca ed ogni valore indica il numero di readers in quella determinata epoca.
-   `unsigned long epoch`: indica l'epoca corrente.
- `struct mutex write_lock`: utilizzato per evitare scritture concorrenti.
- `wait_queue_head_t rcu_wq`: utilizzata per l'attesa dei scrittori in presenza di readers.
> **Note:** *EPOCHS* è una macro configurabile in *rcu.h*, il valore di default è 2.

## System call
### int put_data(char* source, size_t size)
 1. Effettua un check sulla validità dei parametri in input.
 2. Prende il lock `write_lock` per evitare scritture concorrenti.
 3. Incrementa atomicamente con `__sync_fetch_and_add` lo *usage counter* del file system `currentlyInUse`.
 4. Tramite `kmalloc` viene alloca un kernel buffer per accogliere il messaggio utente, copiato con `copy_from_user`.
 5. Inizializza e popola il blocco che deve sostituire il primo invalido (se esiste), ovvero quello ad offset `firstInvalidBlock` (valore mantenuto nel *superblocco*). Nel caso in cui non esista, la system call termina con l’errore *ENOMEM*.
 6. Inizia un nuova epoca RCU e, se sono presenti readers nella precedente, attende la fine delle letture andando in sleep su `rcu_wq` con `wait_event_interruptible`.
 7. Sovrascrive il *blocco selezionato* e aggiorna le liked list di blocchi *validi* e *invalidi*:
  - nel *superblocco*:
    - `firstInvalidBlock` aggiornato con in valore di `nextInvalidBlock` del *blocco selezionato*.
    - `firstValidBlock` aggiornato con l'offset del *blocco selezionato* (ovvero il vecchio valore di `firstInvalidBlock`).
  - nel *blocco selezionato*:
    - `nextInvalidBlock` invalidato (gli viene assegnato -1).
    - `nextValidBlock` aggiornato con il vecchio valore di `firstValidBlock` del *superblocco*.
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
 8. Rilascia il lock `write_lock`.
 9. Decrementa atomicamente con `__sync_fetch_and_sub` lo *usage counter* del file system `currentlyInUse`.

### int get_data(int offset, char* destination, size_t size)
 1. Effettua un check sulla validità dei parametri in input.
 2. Incrementa atomicamente con `__sync_fetch_and_add` lo *usage counter* del file system `currentlyInUse`.
 3. Incrementa atomicamente con `__sync_fetch_and_add` il *readers counter* nei metadati di RCU per l'epoca corrente in `readers[epoch]`.
 4. Accede al blocco richiesto (con `offset+2` per evitare superblocco e inode).
 5. Controlla la validità del blocco dal campo `isValid`, se è invalido la system call termina con l’errore *ENODATA*.
 6. Utilizzando `copy_to_user`, copia `size` bytes del campo `data` del blocco nel buffer `destination`.
 7. Decrementa atomicamente con  `__sync_fetch_and_sub`  lo  _usage counter_  del file system  `currentlyInUse`.
 8. Decrementa atomicamente con `__sync_fetch_and_sub` il *readers counter* nei metadati di RCU per l'epoca corrente in `readers[epoch]`.
 9. Sveglia i thread dormienti sulla `rcu_wq` con `wake_up_interruptible`.

### int invalidate_data(int offset)
 1. Effettua un check sulla validità del parametro in input.
 2. Prende il lock `write_lock` per evitare scritture concorrenti.
 3. Incrementa atomicamente con `__sync_fetch_and_add` lo *usage counter* del file system `currentlyInUse`.
 4. Accede al blocco richiesto (con `offset+2` per evitare superblocco e inode).
 5. Controlla la validità del blocco dal campo `isValid`, se è invalido la system call termina con l’errore *ENODATA*.
 6. Inizia un nuova epoca RCU e, se sono presenti readers nella precedente, attende la fine delle letture andando in sleep su `rcu_wq` con `wait_event_interruptible`.
 7. Aggiorna il *blocco selezionato* e aggiorna le liked list di blocchi *validi* e *invalidi*:
  - nel *superblocco*:
    - `firstInvalidBlock` aggiornato con l'offset del *blocco selezionato*.
  - nel blocco precedente a quello selezionato nella valid linked list, quello con `nextValidBlock` pari all'offset del *blocco selezionato* (nello schema *Valid Block 1*):
    - `nextValidBlock` aggiornato con in valore di `nextValidBlock` del *blocco selezionato*.
  - nel *blocco selezionato*:
    - `isValid` impostato a *False*.
    - `nextInvalidBlock` aggiornato con il vecchio valore di `firstInvalidBlock` del *superblocco*.
    - `nextValidBlock` invalidato (gli viene assegnato -1).
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
 8. Rilascia il lock `write_lock`
 9. Decrementa atomicamente con `__sync_fetch_and_sub` lo *usage counter* del file system `currentlyInUse`. 

## File operation

### int onefilefs_open(struct inode *inode, struct file *file)
1. Incrementa atomicamente con `__sync_fetch_and_add` lo *usage counter* del file system `currentlyInUse`.
2. Controlla con il campo `isMounted` se il dispositivo è montato.
3. Apre il file e controlla se è stato aperto in modalità *read-only*.

### int onefilefs_release(struct inode *inode, struct file *file)
1. Controlla con il campo `isMounted` se il dispositivo è montato.
2. Decrementa atomicamente con `__sync_fetch_and_sub` lo *usage counter* del file system `currentlyInUse`. 
3. Il file viene chiuso

### ssize_t onefilefs_read(struct file * filp, char __user * buf, size_t len, loff_t * off)
1. Controlla con il campo `isMounted` se il dispositivo è montato.
2. Incrementa atomicamente con `__sync_fetch_and_add` il *readers counter* nei metadati di RCU per l'epoca corrente in `readers[epoch]`.
3. Scorrendo la *valid linked list* si copiano dai blocchi i campi `data` per poi riversarli con `copy_to_user` nel buffer `buf`.
4. Decrementa atomicamente con  `__sync_fetch_and_sub`  lo  _usage counter_  del file system  `currentlyInUse`.
5. Decrementa atomicamente con `__sync_fetch_and_sub` il *readers counter* nei metadati di RCU per l'epoca corrente in `readers[epoch]`.
6. Sveglia i thread dormienti sulla `rcu_wq` con `wake_up_interruptible`.