# Progetto SOA: Block-level data management service
## Author
* **Pierpaolo Spaziani** (serial number: 0316331)

## Index
1. [Introduction](#introduction)
2. [Strutture dati utilizzate](#strutture-dati-utilizzate)
3. [Montaggio e smontaggio del file system](#montaggio-e-smontaggio-del-file-system)
4. [System call](#system-call)
5. [File operation](#file-operation)
6. [Sincronizzazione](#sincronizzazione)
7. [Software di livello user](#software-di-livello-user)
8. [Howto](#howto)

## Introduction
Il progetto è dedicato alla creazione di un device driver per sistemi Linux che gestisce i dati a livello di blocchi, in particolare i messaggi degli utenti.
Ogni blocco del dispositivo ha una dimensione di 4KB ed è organizzato nel seguente modo:
-   La metà inferiore (X byte) conserva i dati dell'utente.
-   La metà superiore (4KB-X byte) contiene i metadati necessari per la gestione del dispositivo.

Il driver di dispositivo si basa principalmente su chiamate di sistema supportate parzialmente dal VFS (Virtual File System) e parzialmente no. Le chiamate di sistema implementate sono:

-  `int put_data(char *source, size_t size)`: permette di inserire in un blocco libero del dispositivo, i dati dello spazio utente identificati dal puntatore `source` delle dimensioni specificate in `size`. La systemcall restituisce un intero che rappresenta l'offset del dispositivo (l'indice del blocco) in cui sono stati inseriti i dati. Se attualmente non c'è spazio disponibile sul dispositivo, il servizio restituisce *ENOMEM*.
    
-   `int get_data(int offset, char * destination, size_t size)`: permette di leggere fino a `size` byte dal blocco in un dato `offset`, se attualmente contiene dati validi. La systemcall restituisce un intero che rappresenta la quantità di byte effettivamente caricati nell'area di destinazione o zero se il blocco del dispositivo non contiene dati. In caso di assenza di dati validi associati al parametro `offset`, il servizio restituisce *ENODATA*.
    
-   `int invalidate_data(int offset)`: permette di invalidare i dati di un blocco ad un dato `offset`. La systemcall restituisce *ENODATA* se non ci sono dati validi associati al blocco indicato.

Inoltre, il driver di dispositivo supporta le operazioni del file system che consentono l'accesso ai dati attualmente salvati, tra cui:
-   `open`: l'apertura del dispositivo come flusso di byte.
-   `release`: la chiusura del file associato al dispositivo.
-   `read`: la lettura dei dati in base all'ordine di consegna dei messaggi.

È importante notare che il driver di dispositivo supporta un solo montaggio alla volta. Quando il dispositivo non è montato, sia le operazioni del file system che le chiamate di sistema non supportate dal VFS restituiranno errori, in particolare l'errore *ENODEV*.

Il numero massimo di blocchi gestibili è configurabile al momento della compilazione tramite il parametro `NBLOCKS` presente nel *Makefile*.