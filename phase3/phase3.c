#include "phase1.h"
#include "phase2.h"
#include "phase3.h"
#include "phase3_usermode.h"
#include <usloss.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ShadowProcess {
    int pid;
    int inUse;
    int (*func)(void *);
    int *arg;
    int lock;
    struct ShadowProcess *next;
} ShadowProcess;

typedef struct Sem {
    int slot;
    int inUse;
    int start;
    int currVal;
    struct ShadowProcess *Process;
    int mutex;
    int prev;
} Sem;

// prototypes
void Kernel_Spawn(USLOSS_Sysargs *args);
int Spawn_Helper(void *args);
void Kernel_Wait(USLOSS_Sysargs *args);
void Kernel_Terminate(USLOSS_Sysargs *args);
void Kernel_SemCreate(USLOSS_Sysargs *args);
void Kernel_SemP(USLOSS_Sysargs *args);
void Kernel_SemV(USLOSS_Sysargs *args);
void Kernel_GetTimeofDay(USLOSS_Sysargs *args);
void Kernel_GetPID(USLOSS_Sysargs *args);

// Global arrays
static struct ShadowProcess shadowProcTable[MAXPROC];
static struct Sem semaphoreTable[MAXSEMS];

// Global variables
int semaphoreCount;

/**************
* Function: phase3_init
* Parameters: void
* Returns: void
* Description: Initializes any data structure that will be needed for
*              our program to work.
***************/
void phase3_init() {
    semaphoreCount = 0;

    memset(semaphoreTable, 0, sizeof(semaphoreTable));
    memset(shadowProcTable, 0, sizeof(shadowProcTable));

    systemCallVec[SYS_SPAWN] = (void *) Kernel_Spawn;
    systemCallVec[SYS_WAIT] = (void *) Kernel_Wait;
    systemCallVec[SYS_TERMINATE] = (void *) Kernel_Terminate;
    systemCallVec[SYS_SEMCREATE] = (void *) Kernel_SemCreate;
    systemCallVec[SYS_SEMP] = (void *) Kernel_SemP;
    systemCallVec[SYS_SEMV] = (void *) Kernel_SemV;
    systemCallVec[SYS_GETPID] = (void *) Kernel_GetPID;
    systemCallVec[SYS_GETTIMEOFDAY] = (void *) Kernel_GetTimeofDay;
}

void phase3_start_service_processes() {

}

void Kernel_Spawn(USLOSS_Sysargs *args) {
    // in kernel mode
    // unpack all of the args into variables
    int (*func)(void*) = (int (*)(void*))args->arg1;
    void *arg = args->arg2;
    int stack_size = (int)(long)args->arg3;
    int priority = (int)(long)args->arg4;
    char *name = (char*)args->arg5;

    // call spork(func_main) creates child process, but imagine func_main() executes
    int childPid = spork(name, Spawn_Helper, arg, stack_size, priority);
    int pid = getpid();
    
    if(!shadowProcTable[childPid % MAXPROC].inUse) {
        shadowProcTable[childPid % MAXPROC].inUse = 1;
        shadowProcTable[childPid % MAXPROC].pid = pid;
        shadowProcTable[childPid % MAXPROC].lock = MboxCreate(0,0);
        shadowProcTable[childPid % MAXPROC].func = func;
        MboxRecv(shadowProcTable[childPid % MAXPROC].lock, 0, 0);
    }
    else {
        shadowProcTable[childPid % MAXPROC].func = func;
        MboxSend(shadowProcTable[childPid % MAXPROC].lock, 0, 0);
    }

    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);

    args->arg1 = (void*)(long)childPid;
    args->arg4 = (void*)(long)0;
}

int Spawn_Helper(void *args) {
    // get the pid of the child process
    int pid = getpid();
    if(shadowProcTable[pid % MAXPROC].inUse == 0) {
        shadowProcTable[pid % MAXPROC].inUse = 1;
        shadowProcTable[pid % MAXPROC].pid = pid;
        shadowProcTable[pid % MAXPROC].lock = MboxCreate(0,0);
        MboxRecv(shadowProcTable[pid % MAXPROC].lock, 0, 0);
    }
    else {
        MboxSend(shadowProcTable[pid % MAXPROC].lock, 0, 0);
    }
    int (*_func)(void*) = shadowProcTable[pid % MAXPROC].func;
    void *_arg = args;

    // Enter user mode here
    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);

    int retval = _func(_arg);

    // Terminate when done (should never reach the return statement)
    Terminate(retval);
    return 0;
}

void Kernel_Wait(USLOSS_Sysargs *args) {
    int *status = args->arg2;
    int pid = join(&status);

    args->arg1 = (void*)(long)pid;
    args->arg2 = (void*)(long)status;
    args->arg4 = (void*)(long)0;

    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}

void Kernel_Terminate(USLOSS_Sysargs *args) {
    int status = (int)(long)args->arg1;
    int temp = status;
    int pid = getpid();
    while(1) {
        int kidPid = join(&temp);
        if(kidPid == -2) {
            break;
        }
    }
    
    quit(status);
    // make sure we are in user mode
    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}

void Kernel_SemCreate(USLOSS_Sysargs *args) {
    // make sure we are in kernel mode
    int val = (long)args->arg1;
    if(val < 0 || semaphoreCount == MAXSEMS) {
        args->arg4 = (void*)(long)-1;
    }
    else {
        semaphoreCount++;
        int prev = MboxCreate(val, 0);
        int mutex = MboxCreate(1, 0);
        MboxSend(mutex, NULL, 0);
        int i;
        for(i=0; i<MAXSEMS; i++) {
            if(semaphoreTable[i].inUse == 0) {
                semaphoreTable[i].inUse = 1;
                semaphoreTable[i].slot = i;
                semaphoreTable[i].start = val;
                semaphoreTable[i].currVal = val;
                semaphoreTable[i].prev = prev;
                semaphoreTable[i].mutex = mutex;
                break;
            }
        }

        // DON'T DO THIS. Don't fill up prev with a bunch of messages. Do not use messages as tokens. 
        // Only use mailbox only for wakeup
        int j;
        for(j=0; j<val; j++) {
            MboxSend(prev, NULL, 0);
        }
        MboxRecv(mutex, NULL, 0);
        args->arg1 = (void*)(long)semaphoreTable[i].slot;
        args->arg4 = 0;
    }

    // put into user mode
    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}

void Kernel_SemP(USLOSS_Sysargs *args) {
    int val = (long)args->arg1;
    if(val < 0) {
        printf("Semaphore does not exist | setting it to invalid\n");
        args->arg4 = (void*)(long)-1;
    }
    else {
        args->arg4 = 0;  // set as valid
        MboxSend(semaphoreTable[val].mutex, NULL, 0);
        if(semaphoreTable[val].currVal == 0) {
            // add current process to blocked processes queue
            MboxRecv(semaphoreTable[val].mutex, NULL, 0);  // release mutex
            MboxRecv(semaphoreTable[val].prev, NULL, 0);
            MboxSend(semaphoreTable[val].mutex, NULL, 0);
        }
        else {
            printf("P'ing semaphore %d\n", val);
            semaphoreTable[val].currVal--;
            MboxRecv(semaphoreTable[val].prev, NULL, 0);
        }
        MboxRecv(semaphoreTable[val].mutex, NULL, 0);
    }


    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}

void Kernel_SemV(USLOSS_Sysargs *args) {
    int val = (long)args->arg1;
    if(val<0) {
        args->arg4 = (void*)(long)-1;
    }
    else {
        args->arg4 = 0;
    }
    MboxSend(semaphoreTable[val].mutex, NULL, 0);
    if(semaphoreTable[val].Process != NULL) {
        // Dequeue the process
        MboxRecv(semaphoreTable[val].mutex, NULL, 0);
        MboxSend(semaphoreTable[val].prev, NULL, 0);
        MboxSend(semaphoreTable[val].mutex, NULL, 0);
    }
    else {
        printf("V'ing semaphore %d\n", val);
        semaphoreTable[val].currVal++;

        // need to wake up blocked process
        MboxRecv(semaphoreTable[val].mutex, NULL, 0);
        MboxSend(semaphoreTable[val].mutex, NULL, 0);

    }
    MboxRecv(semaphoreTable[val].mutex, NULL, 0);

    USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE);
}

void Kernel_GetTimeofDay(USLOSS_Sysargs *args) {
    // make sure we are in kernel mode
    args->arg1 = currentTime();
}

void Kernel_GetPID(USLOSS_Sysargs *args) {
    // make sure we are in kernel mode
    args->arg1 = getpid();
}