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


// Prototypes
int TerminalDriver(char *arg);
void sleepSysHandler(USLOSS_Sysargs *args);
int Kernel_Sleep(int time);
void termReadSysHandler(USLOSS_Sysargs *args);
void termWriteSysHandler(USLOSS_Sysargs *args);
int Kernel_TermRead(char *buffer, int bufferSize, int unit, int *charsRead);
int Kernel_TermWrite(char *buff, int buffSize, int unit, int *charWrite);
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

/* 
*  Function: phase4_init
*  Initializes the phase 4 system calls and data structures.
*/
void phase4_init(void) {
    systemCallVec[SYS_SLEEP] = sleepSysHandler;
    systemCallVec[SYS_TERMREAD] = termReadSysHandler;
    systemCallVec[SYS_TERMWRITE] = termWriteSysHandler;

    memset(sleeping, 0, sizeof(sleeping));

    // Create mailboxes for each terminal unit to store read buffers (up to 10)
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        TermReadBoxes[i] = MboxCreate(10, MAXLINE);  // create returns maibox id
        TermWriteBoxes[i] = MboxCreate(0, 0);
        TermWriteLocks[i] = MboxCreate(1, 0);
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


// Lock and Unlock functions
void lock(int lockId) {
    MboxSend(lockId, NULL, 0);
}

void unlock(int lockId) {
    MboxRecv(lockId, NULL, 0);
}
