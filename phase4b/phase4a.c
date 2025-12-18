#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase4.h"
#include "phase3_usermode.h"
#include "phase3_kernelInterfaces.h"
#include "phase4_usermode.h"
#include <usloss.h>
#include "usloss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
  Authors: Hudson Cox (majority contribution), Amelia Matheson
  File: phase4a.c
*/

// Prototypes
void Kernel_sleep(USLOSS_Sysargs *args);
void Kernel_TermRead(USLOSS_Sysargs *args);
void Kernel_TermWrite(USLOSS_Sysargs *args);
void Kernel_DiskRead(USLOSS_Sysargs *args);
void Kernel_DiskWrite(USLOSS_Sysargs *args);
void Kernel_DiskSize(USLOSS_Sysargs *args);

// Tables

void phase4_init() {
    systemCallVec[SYS_SLEEP] = (void *) Kernel_sleep;
    systemCallVec[SYS_TERMREAD] = (void *) Kernel_TermRead;
    systemCallVec[SYS_TERMWRITE] = (void *) Kernel_TermWrite;
    systemCallVec[SYS_DISKREAD] = (void *) Kernel_DiskRead;
    systemCallVec[SYS_DISKWRITE] = (void *) Kernel_DiskWrite;
    systemCallVec[SYS_DISKSIZE] = (void *) Kernel_DiskSize;
}

void phase4_start_service_processes() {}

void Kernel_sleep(USLOSS_Sysargs *args) {
    // make sure we are in kernel mode
    int time = args->arg1;
    if (time < 0) {
        args->arg4 = -1;
    }
    else {
        int waitTime = (currentTime()/1000) + (time*1000);
        int status;
        while (currentTime()/1000 < waitTime) {
            waitDevice(0, 0, &status);
        }
        args->arg4 = 0;
    }
}

void Kernel_TermRead(USLOSS_Sysargs *args) {
    // Extract info from args
    char *buff = args->arg1;
    int len = args->arg2;
    int unit = args->arg3;
    if (unit < 0 || unit >= 4 || buff == NULL || len <= 0) {
        args->arg4 = -1;
        return;
    }

    int count = 0;
    int status;
    int result;

    while (count < len) {
        /*
        // Infinite loop (not working at the moment)
        while (1) {
            result = USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &status);
            if (result != USLOSS_DEV_OK) {
                args->arg4 = -1;
            }
            if ((USLOSS_TERM_STAT_RECV(status) & USLOSS_DEV_BUSY) != 0) {
                break;
            }
        }
        */
        char receivedChar = USLOSS_TERM_STAT_CHAR(status);
        buff[count++] = receivedChar;
        if (receivedChar == '\n' || count == len) {
            break;
        }
    }
    if (count < len) {
        buff[count] = '\0';
    }

    args->arg4 = count;
}

void Kernel_TermWrite(USLOSS_Sysargs *args) {
    printf("Kernel_TermWrite\n");
    
    char* buff = args->arg1;
    int len = args->arg2;
    int unit = args->arg3;
    if (unit < 0 || unit >= 4 || buff == NULL || len < 0) {
        args->arg4 = -1;
        return;
    }

    int count = 0;
    int status;
    int result;

    for (int i = 0; i < len; i++) {
        /*
        // Infinite loop (not working at the moment)
        while (1) {
            result = USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &status);
            if (result != USLOSS_DEV_OK) {
                args->arg4 = -1;
                return;
            }
            if ((USLOSS_TERM_STAT_XMIT(status) & USLOSS_DEV_READY) != 0) {
                break;
            }
        }
        */
        int control = 0;
        control = USLOSS_TERM_CTRL_CHAR(control, buff[i]);
        control = USLOSS_TERM_CTRL_XMIT_INT(control);
        control = USLOSS_TERM_CTRL_XMIT_CHAR(control);

        result = USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, control);
        if (result != USLOSS_DEV_OK) {
            args->arg4 = -1;
            return;
        }
        count++;
    }
    args->arg4 = count;
}

void Kernel_DiskRead(USLOSS_Sysargs *args) {
    // make sure we are in kernel mode
    // read with USLOSS_DeviceInput
}

void Kernel_DiskWrite(USLOSS_Sysargs *args) {
    // make sure we are in kernel mode
    // write with USLOSS_DeviceOutput
}

void Kernel_DiskSize(USLOSS_Sysargs *args) {
    // make sure we are in kernel mode
    int unit = args->arg1;
    if (unit < 0 || unit >= USLOSS_DISK_UNITS) {
        args->arg4 = -1;
        return;
    }

    /*
    USLOSS_DeviceRequest req;
    req.opr = ;
    req.reg1 = (void*)(long)blockInd

    int blockBytes;
    int trackBlocks;
    int diskTracks;
    int result = USLOSS_DeviceRequest(USLOSS_DISK_DEV, unit, &blockBytes, &trackBlocks, &diskTracks);
    if (result != USLOSS_DEV_OK) {
        args->arg4 = -1;
        return;
    }
    args->arg1 = blockBytes;
    args->arg2 = trackBlocks;
    args->arg3 = diskTracks;
    args->arg4 = 0;*/

}