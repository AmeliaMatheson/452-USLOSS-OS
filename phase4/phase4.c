#include <stdio.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*
  Authors: Hudson Cox, Amelia Matheson
  File: phase4.c
*/

// Structs
typedef struct SleepingProc {
    int pid;
    int wakeupTime;
    struct SleepingProc *next;
} SleepingProc;

typedef struct DiskRequest DiskRequest;

struct DiskRequest {
    int operation;
    int track;
    DiskRequest *next;
};

typedef struct Disk {
    USLOSS_DeviceRequest request;
    int unit;
    int tracks;
    int track_size;
    int current_track;
    int sector_size;
    int disk_size;
    int status;
    DiskRequest *requestQueue;
} Disk;

// Prototypes
int TerminalDriver(char *arg);
void sleepSysHandler(USLOSS_Sysargs *args);
int Kernel_Sleep(int time);
void termReadSysHandler(USLOSS_Sysargs *args);
void termWriteSysHandler(USLOSS_Sysargs *args);
int Kernel_TermRead(char *buffer, int bufferSize, int unit, int *charsRead);
int Kernel_TermWrite(char *buff, int buffSize, int unit, int *charWrite);
void diskReadSysHandler(USLOSS_Sysargs *args);
void diskWriteSysHandler(USLOSS_Sysargs *args);
void diskSizeSysHandler(USLOSS_Sysargs *args);
int Kernel_DiskRead(void *buffer, int unit, int track, int firstBlock, int blocks, int *status);
int Kernel_DiskWrite(void *buffer, int unit, int track, int firstBlock, int blocks, int *status);
int Kernel_DiskSize(int unit, int *sector, int *track, int *disk);
int DiskDriver(char *arg);
int ClockDriver(char *arg);

void lock(int lockId);
void unlock(int lockId);

// Global variables
int clockTicks = 0;
int sleepLock;

// Tables and Queues
int TermReadBoxes[USLOSS_TERM_UNITS];
int TermWriteLocks[USLOSS_TERM_UNITS];
int TermWriteBoxes[USLOSS_TERM_UNITS];

SleepingProc sleeping[MAXPROC];
SleepingProc *sleepingQueue = NULL;

Disk disks[USLOSS_DISK_UNITS];
int diskLocks[USLOSS_DISK_UNITS];
int DiskRequestBoxes[USLOSS_DISK_UNITS];


/* 
*  Function: phase4_init
*  Initializes the phase 4 system calls and data structures.
*/
void phase4_init(void) {
    systemCallVec[SYS_SLEEP] = sleepSysHandler;
    systemCallVec[SYS_TERMREAD] = termReadSysHandler;
    systemCallVec[SYS_TERMWRITE] = termWriteSysHandler;
    systemCallVec[SYS_DISKSIZE] = diskSizeSysHandler;
    systemCallVec[SYS_DISKREAD] = diskReadSysHandler;
    systemCallVec[SYS_DISKWRITE] = diskWriteSysHandler;

    memset(sleeping, 0, sizeof(sleeping));

    // Create mailboxes for each terminal unit to store read buffers (up to 10)
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        TermReadBoxes[i] = MboxCreate(10, MAXLINE);  // create returns maibox id
        TermWriteBoxes[i] = MboxCreate(0, 0);
        TermWriteLocks[i] = MboxCreate(1, 0);
    }

    // Initialize the two disk structs and their mailboxes
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        disks[i].unit = i;
        disks[i].tracks = 0;
        disks[i].track_size = 0;
        disks[i].sector_size = 0;
        disks[i].disk_size = 0;
        disks[i].status = 0;
        disks[i].request.opr = -1;
        disks[i].request.reg1 = (void *)(long)-1;
        disks[i].request.reg2 = (void *)(long)-1;
        disks[i].current_track = -1;
        diskLocks[i] = MboxCreate(1, 0);
        DiskRequestBoxes[i] = MboxCreate(1, 0);
    }

    // Create lock for sleep handler
    sleepLock = MboxCreate(1, 0);

    // Enable interrupts for terminal units
    int control = 0;
    control = USLOSS_TERM_CTRL_XMIT_INT(control);
    control = USLOSS_TERM_CTRL_RECV_INT(control);

    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 0, (void *)(long)control);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 1, (void *)(long)control);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 2, (void *)(long)control);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, 3, (void *)(long)control);
}

/*
*  Function: phase4_start_service_processes
*  Starts the phase 4 service processes, including the clock device driver and
*  the terminal device drivers for each of the four terminal units
*/
void phase4_start_service_processes(void) {
    // start terminal device drivers for each unit, will act as a waiting process for interrupts
    spork("TerminalDriver0", TerminalDriver, "0", USLOSS_MIN_STACK, 2);
    spork("TerminalDriver1", TerminalDriver, "1", USLOSS_MIN_STACK, 2);
    spork("TerminalDriver2", TerminalDriver, "2", USLOSS_MIN_STACK, 2);
    spork("TerminalDriver3", TerminalDriver, "3", USLOSS_MIN_STACK, 2);

    // start clock device driver, will act as a waiting process for interrupts
    spork("ClockDriver", ClockDriver, NULL, USLOSS_MIN_STACK, 1);

    // start disk device drivers for each unit, will act as a waiting process for interrupts
    spork("DiskDriver1", DiskDriver, "0", USLOSS_MIN_STACK, 1);
    spork("DiskDriver2", DiskDriver, "1", USLOSS_MIN_STACK, 1);
}

/*
* Function: TerminalDriver
* Handles terminal interrupts for a specific terminal unit
* @param arg: the terminal unit to handle
* @return 0
*/
int TerminalDriver(char *arg) {
    int unit = atoi(arg);
    int status;

    char buffer[MAXLINE] = "";
    int count = 0;

    // Infinite loop to handle terminal interrupts
    while (1) {
        // Call wait device for this terminal unit
        waitDevice(USLOSS_TERM_DEV, unit, &status);

        // Check if the terminal is ready to read
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
            char receivedChar = USLOSS_TERM_STAT_CHAR(status);
            buffer[count] = receivedChar;
            count += 1;

            // Send buffer if encounter newline or buffer is full. Reset buffer and count
            if (receivedChar == '\n' || count == MAXLINE) {
                MboxCondSend(TermReadBoxes[unit], buffer, count);
                strcpy(buffer, "");
                count = 0;
            }
        }

        // Check if the terminal is ready to write
        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            // Send a message to the write request mailbox
            MboxCondSend(TermWriteBoxes[unit], NULL, 0);
        }
    }
    return 0;
}

/*
* Function: ClockDriver
* Handles clock interrupts
* @param arg: unused
* @return 0
*/
int ClockDriver(char *arg) {
    int status;
    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);

        lock(sleepLock);
        clockTicks++;

        // Wake up any processes whose sleep time has expired
        while (sleepingQueue != NULL && sleepingQueue->wakeupTime <= clockTicks) {
            SleepingProc *toWake = sleepingQueue;
            unblockProc(toWake->pid);
            sleepingQueue = sleepingQueue->next;
        }

        unlock(sleepLock);
    }

    return 0;
}

/*
* Function: sleepSysHandler
* Handles the sleep system call
* @param args: the system arguments
*/
void sleepSysHandler(USLOSS_Sysargs *args) {

    // Extract arguments
    int sec = (int)(long)args->arg1;
    
    // Call Kernel_sleep
    int sysStat = Kernel_Sleep(sec);
    args->arg4 = (void *)(long)sysStat;
}

/*
* Function: Kernel_Sleep
* Puts the current process to sleep for a specified amount of time
* @param time: the number of seconds to sleep
*/
int Kernel_Sleep(int time) {
    // make sure we are in kernel mode
    
    lock(sleepLock);
    if (time < 0) {
        return -1;
    }

    int waitTime = clockTicks + (time * 10);
    int status;
    int pid = getpid();

    // create new sleeping process
    SleepingProc *newSleep = &sleeping[pid % MAXPROC];
    newSleep->pid = pid;
    newSleep->wakeupTime = waitTime;
    newSleep->next = NULL;

    // add to the queue
    if (sleepingQueue == NULL) {
        sleepingQueue = newSleep;
    }
    else {
        // insert in order
        SleepingProc *prev = NULL;
        SleepingProc *current = sleepingQueue;
        while (current != NULL && current->wakeupTime <= newSleep->wakeupTime) {
            prev = current;
            current = current->next;
        }
        if (prev == NULL) {
            newSleep->next = sleepingQueue;
            sleepingQueue = newSleep;
        }
        else {
            newSleep->next = prev->next;
            prev->next = newSleep;
        }
    }

    unlock(sleepLock);
    blockMe();
    return 0;
}

/*
* Function: Kernel_TermRead
* Reads characters from the terminal mailbox
* @param buff: the buffer to read into
* @param buffSize: the size of the buffer
* @param unit: the terminal unit to read from
* @param charsRead: the number of characters read
* @return 0 on success, -1 on failure
*/
int Kernel_TermRead(char *buff, int buffSize, int unit, int *charsRead) {
    if (unit < 0 || unit >= USLOSS_TERM_UNITS || buff == NULL || buffSize <= 0) {
        return -1;
    }

    // Expecting that interrupts are being handled in the terminal driver, just need
    // to grab the characters from the mailbox
    char readin[MAXLINE + 1];
    *charsRead = MboxRecv(TermReadBoxes[unit], readin, MAXLINE);

    memcpy(buff, readin, buffSize);  // use buffSize to copy only the number of characters requested
    if (*charsRead > buffSize) {
        *charsRead = buffSize;  // only copy the number of characters requested
    }
    buff[*charsRead] = '\0';  // null terminate the string
    return 0;
}


/*
* Function: termReadSysHandler
* Handles the terminal read system call
* @param args: the system arguments
*/
void termReadSysHandler(USLOSS_Sysargs *args) {
    // Extract arguments
    char *buff = (char *)args->arg1;
    int buffSize = (int)(long)args->arg2;
    int unit = (int)(long)args->arg3;

    // Grab the characters from the mailbox
    int charRead = 0;
    int sysStat = Kernel_TermRead(buff, buffSize, unit, &charRead);

    args->arg2 = (void *)(long)charRead;
    args->arg4 = (void *)(long)sysStat;
}

/*
* Function: Kernel_TermWrite
* Writes characters to the terminal
* @param buff: the buffer to write from
* @param buffSize: the size of the buffer
* @param unit: the terminal unit to write to
* @param charWrite: the number of characters written
* @return 0 on success, -1 on failure
*/
int Kernel_TermWrite(char *buff, int buffSize, int unit, int *charWrite) {
    if (unit < 0 || unit >= USLOSS_TERM_UNITS || buff == NULL || buffSize <= 0) {
        return -1;
    }

    // Write the characters to the terminal
    for (int i = 0; i < buffSize; i++) {
        MboxRecv(TermWriteBoxes[unit], NULL, 0);

        int control = 0x1;
        control |= 0x2;
        control |= 0x4;
        control |= (buff[i] << 8);

        if (USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)control) != USLOSS_DEV_OK) {
            return -1;
        }
        *charWrite += 1;
    }
    return 0;
}

/*
* Function: termWriteSysHandler
* Handles the terminal write system call
* @param args: the system arguments
*/
void termWriteSysHandler(USLOSS_Sysargs *args) {
    // Extract arguments
    char *buff = (char *)args->arg1;
    int buffSize = (int)(long)args->arg2;
    int unit = (int)(long)args->arg3;

    // Grab the characters from the mailbox
    int charWrite = 0;

    lock(TermWriteLocks[unit]);
    int sysStat = Kernel_TermWrite(buff, buffSize, unit, &charWrite);
    unlock(TermWriteLocks[unit]);

    args->arg2 = (void *)(long)charWrite;
    args->arg4 = (void *)(long)sysStat;
}


// DISK FUNCTIONALITY ADDONS FOR PHASE 4B

/*
* Function: DiskDriver
* Handles disk interrupts for a specific disk unit
* @param arg: the disk unit to handle
* @return 0
*/
int DiskDriver(char *arg) {
    int unit;
    int status1;
    int status2;

    if (strcmp(arg, "0") == 0) {
        unit = 0;
    }
    else if (strcmp(arg, "1") == 0) {
        unit = 1;
    }

    // Infinite loop to handle disk interrupts
    while (1) {
        waitDevice(USLOSS_DISK_DEV, unit, &status1);  // wait for disk interrupt

        // check if disk is ready to process a new request
        USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &status2);  
        if (status2 == USLOSS_DEV_READY) {
            MboxCondSend(DiskRequestBoxes[unit], NULL, 0);
        }
    }
    return 0;
}

/*
* Function: Kernel_DiskRead
* Reads blocks from the disk
* @param buffer: the buffer to read into
* @param unit: the disk unit to read from
* @param track: the track to read from
* @param firstBlock: the first block to read from
* @param blocks: the number of blocks to read
* @param status: the status of the disk operation
* @return 0 on success, -1 on failure
*/
int Kernel_DiskRead(void *buffer, int unit, int track, int firstBlock, int blocks, int *status) {
    // Error checking
    if (unit < 0 || unit >= USLOSS_DISK_UNITS || buffer == NULL || blocks <= 0 || track < 0 || track >= USLOSS_DISK_TRACK_SIZE || firstBlock < 0 || firstBlock >= USLOSS_DISK_TRACK_SIZE) {
        return -1;
    }

    lock(diskLocks[unit]);

    disks[unit].request.opr = USLOSS_DISK_SEEK;  // use seek operation to move to the correct track
    disks[unit].request.reg1 = (void *)(long)track;  // set the track to move to
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);
    MboxRecv(DiskRequestBoxes[unit], NULL, 0);  // wait for the seek operation to complete

    // Read the blocks from the disk
    char readBuffer[USLOSS_DISK_SECTOR_SIZE];
    int charsRead = 0;
    for (int i = firstBlock; i < firstBlock + blocks; i++) {
        int current_track = track + i / USLOSS_DISK_TRACK_SIZE;
        int current_block = i % USLOSS_DISK_TRACK_SIZE;

        if (disks[unit].current_track != current_track) {
            disks[unit].current_track = current_track;
            disks[unit].request.opr = USLOSS_DISK_SEEK;
            disks[unit].request.reg1 = (void *)(long)disks[unit].current_track;
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);
            MboxRecv(DiskRequestBoxes[unit], NULL, 0);
        }

        disks[unit].request.opr = USLOSS_DISK_READ;
        disks[unit].request.reg1 = (void *)(long)current_block;
        disks[unit].request.reg2 = readBuffer;  

        USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);

        MboxRecv(DiskRequestBoxes[unit], NULL, 0);
        memcpy((char *)buffer + charsRead, readBuffer, USLOSS_DISK_SECTOR_SIZE);

        charsRead += USLOSS_DISK_SECTOR_SIZE;
    }

    // Get the status of the disk operation
    USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &status); 

    unlock(diskLocks[unit]);

    return 0;
}

/*
* Function: diskReadSysHandler
* Handles the disk read system call
* @param args: the system arguments
*/
void diskReadSysHandler(USLOSS_Sysargs *args) {
    // Extract arguments
    char *buffer = (char *)args->arg1;
    int blocks = (int)(long)args->arg2;
    int track = (int)(long)args->arg3;
    int firstBlock = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;
    int status = 0;

    // Error checking
    if (unit < 0 || unit >= USLOSS_DISK_UNITS) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = (void *)(long)-1;
        return;
    }
    if (buffer == NULL || blocks <= 0) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = (void *)(long)-1;
        return;
    }
    if (track < 0 || track >= USLOSS_DISK_TRACK_SIZE) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = 0;
        return;
    }
    if (firstBlock < 0 || firstBlock >= USLOSS_DISK_TRACK_SIZE) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = (void *)(long)-1;
        return;
    }

    // Read the blocks from the disk

    int res = Kernel_DiskRead(buffer, unit, track, firstBlock, blocks, &status);

    args->arg1 = (void *)(long)status;
    args->arg4 = (void *)(long)res;
}

/*
* Function: Kernel_DiskWrite
* Writes blocks to the disk
* @param buffer: the buffer to write from
* @param unit: the disk unit to write to
* @param track: the track to write to
* @param firstBlock: the first block to write to
* @param blocks: the number of blocks to write
* @param status: the status of the disk operation
* @return 0 on success, -1 on failure
*/
int Kernel_DiskWrite(void *buffer, int unit, int track, int firstBlock, int blocks, int *status) {
    lock(diskLocks[unit]);

    disks[unit].request.opr = USLOSS_DISK_TRACKS;  // set the operation to get the number of tracks on the disk
    disks[unit].request.reg1 = &disks[unit].tracks;  // set the reg1 to store the number of tracks
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);
    MboxRecv(DiskRequestBoxes[unit], NULL, 0);

    if (track >= disks[unit].tracks) {
        unlock(diskLocks[unit]);
        return USLOSS_DEV_ERROR;
    }

    disks[unit].request.opr = USLOSS_DISK_SEEK;
    disks[unit].request.reg1 = (void *)(long)track;  // set the track to move to

    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);
    MboxRecv(DiskRequestBoxes[unit], NULL, 0);

    // Write the blocks to the disk
    int charsWrite = 0;
    for (int i = firstBlock; i < firstBlock + blocks; i++) {
        int current_track = track + i / USLOSS_DISK_TRACK_SIZE;
        int current_block = i % USLOSS_DISK_TRACK_SIZE;

        if (disks[unit].current_track != current_track) {
            disks[unit].current_track = current_track;
            disks[unit].request.opr = USLOSS_DISK_SEEK;
            disks[unit].request.reg1 = (void *)(long)disks[unit].current_track;
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);
            MboxRecv(DiskRequestBoxes[unit], NULL, 0);  // wait for seek to complete
        }

        // Write data to disk
        char writing[USLOSS_DISK_SECTOR_SIZE];
        memcpy(writing, (char *)buffer + charsWrite, USLOSS_DISK_SECTOR_SIZE);
        disks[unit].request.opr = USLOSS_DISK_WRITE;
        disks[unit].request.reg1 = (void *)(long)current_block;
        disks[unit].request.reg2 = writing;

        USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);

        MboxRecv(DiskRequestBoxes[unit], NULL, 0);

        charsWrite += USLOSS_DISK_SECTOR_SIZE;
    }
    USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &status);

    unlock(diskLocks[unit]);
    return 0;
}

/*
* Function: diskWriteSysHandler
* Handles the disk write system call
* @param args: the system arguments
*/
void diskWriteSysHandler(USLOSS_Sysargs *args) {
    // Extract arguments
    char *buffer = (char *)args->arg1;
    int blocks = (int)(long)args->arg2;
    int track = (int)(long)args->arg3;
    int firstBlock = (int)(long)args->arg4;
    int unit = (int)(long)args->arg5;
    int status = 0;

    // Error checking
    if (unit < 0 || unit >= USLOSS_DISK_UNITS) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = (void *)(long)-1;
        return;
    }
    if (buffer == NULL || blocks <= 0) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = (void *)(long)-1;
        return;
    }
    if (track < 0) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = -1;
        return;
    }
    if (firstBlock < 0 || firstBlock > USLOSS_DISK_TRACK_SIZE) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = (void *)(long)-1;
        return;
    }

    // Write the blocks to the disk
    int sysStat = Kernel_DiskWrite(buffer, unit, track, firstBlock, blocks, &status);
    if (sysStat == USLOSS_DEV_ERROR) {
        args->arg1 = (void *)(long)USLOSS_DEV_ERROR;
        args->arg4 = (void *)(long)0;
        return;
    }

    args->arg1 = (void *)(long)status;
    args->arg4 = (void *)(long)sysStat;
}

/*
* Function: Kernel_DiskSize
* Gets the size of the disk
* @param unit: the disk unit to get the size of
* @param sector: the number of sectors on the disk
* @param track: the number of tracks on the disk
* @param disk: the size of the disk
* @return 0 on success, -1 on failure
*/
int Kernel_DiskSize(int unit, int *sector, int *track, int *disk) {
    lock(diskLocks[unit]);

    disks[unit].request.opr = USLOSS_DISK_TRACKS;  // set the operation to get the number of tracks on the disk
    disks[unit].request.reg1 = &disks[unit].tracks;  // set the reg1 to store the number of tracks

    // Send the request to the disk
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &disks[unit].request);
    MboxRecv(DiskRequestBoxes[unit], NULL, 0);
 
    // Set out parameters and disk struct values
    *sector = disks[unit].sector_size = USLOSS_DISK_SECTOR_SIZE;
    *track = disks[unit].track_size = USLOSS_DISK_TRACK_SIZE;
    *disk = disks[unit].disk_size = disks[unit].tracks;

    unlock(diskLocks[unit]);

    return 0;
}

/*
* Function: diskSizeSysHandler
* Handles the disk size system call
* @param args: the system arguments
*/
void diskSizeSysHandler(USLOSS_Sysargs *args) {
    int unit = (int)(long)args->arg1;
    int sector, track, disk;

    int sysStat = Kernel_DiskSize(unit, &sector, &track, &disk);

    args->arg1 = (void *)(long)sector;
    args->arg2 = (void *)(long)track;
    args->arg3 = (void *)(long)disk;
    args->arg4 = (void *)(long)sysStat;
}

// Lock and Unlock functions
void lock(int lockId) {
    MboxSend(lockId, NULL, 0);
}

void unlock(int lockId) {
    MboxRecv(lockId, NULL, 0);
}
