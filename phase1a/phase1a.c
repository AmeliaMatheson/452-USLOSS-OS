#include <phase1.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <usloss.h>
#include <string.h>

/**************
* file: phase1.c
* Authors: Amelia Matheson, Hudson Cox
* Description: This file is part A of phase 1 of our project. It contains the necessary structures to form and maintain
*              A PCB table and monitor relationships throughout the table as processes are added, moved, or removed. It
*              will both initialize the table and create the initial process that will be responsible for starting up
*              processes that follow.
***************/

/**************
* struct: process
* Fields: pid, name, (startFunc)(void), arg, stack_size, priority, status, context.
* Relations: run_queue_next, my_parent, first_child, next_sibling, prev_sibling, first_dead_child, next_dead_child.
* Description: This is the process struct which will be used to hold all important information about a process.
*              These structs will be stored in our array called PCB and each will represent a separate process
*              that is running or waiting to run.
***************/
struct process {
    int pid;
    char name[MAXNAME];
    int (*startFunc)(void*);
    void *arg;
    char* stack;
    int stack_size;
    int priority;
    int status;
    USLOSS_Context context;
    struct process *run_queue_next;
    struct process *my_parent;
    struct process *first_child;
    struct process *next_sibling;
    struct process *prev_sibling;
    struct process *first_dead_child;
    struct process *next_dead_child;
};

// These are the global variables for this phase
struct process PCB[MAXPROC];
struct process *running_proc;
int PIDcounter = 1;
int *retval;
struct process pri1Q;
struct process pri2Q;
struct process pri3Q;
struct process pri4Q;
struct process pri5Q;
struct process pri6Q;

// These are the function prototypes for this phase
void phase1_init(void);
int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority);
int join(int *status);
void quit_phase_1a(int status, int switchToPid);
void TEMP_switchTo(int pid);
void dumpProcesses(void);
void trampoline();
int disableInterrupts();
void init();


/**************
* Function: phase1_init
* Parameters: void
* Returns: void
* Description: This function will initialize our array called PCB and then will insert our first process called init
*              into the PCB table anf initialize all of its fields
***************/
void phase1_init(void) {
    // Disable interrupts
    int oldPSR = disableInterrupts();

    // Initializes all the processes in the PCB table
    memset(PCB, 0, sizeof(PCB));

    // Initializes the init process
    int slotNum = 1;
    PCB[slotNum].pid = 1;
    strcpy(PCB[slotNum].name, "init");
    PCB[slotNum].stack_size = USLOSS_MIN_STACK;
    PCB[slotNum].stack = malloc(USLOSS_MIN_STACK);
    PCB[slotNum].priority = 6;
    PCB[slotNum].startFunc = init;
    USLOSS_ContextInit(&PCB[slotNum].context, PCB[slotNum].stack, PCB[slotNum].stack_size, NULL, trampoline);

    // Set the running process to NULL
    running_proc = NULL;

    // Restore interrupts
    USLOSS_PsrSet(oldPSR);
}

/**************
* Function: spork
* Parameters: char *name, int (*startFunc)(void*), char *arg, int stackSize, int priority 
* Returns: integer
* Description: This function is responsible for finding an open space in the PCB table for a new process and then
*              creating and adding a new process to the table. The arguments that are passed to the function will
*              be used to fill in the fields of the new process in the struct at the empty pid location in the
*              table. It will make the current running process its parent and then make itself the first child of
*              list of children for its new parent.
***************/
int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) {
    // Check if not in kernel mode
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);
    }

    // Disable interrupts
    int oldPSR = disableInterrupts();

    // Error checking
    if (stackSize < USLOSS_MIN_STACK) {
        return -2;
    }
    if (name == NULL || strlen(name) >= (MAXNAME-1)) {
        return -1;
    }
    if (priority < 1 || priority > 5) {
        return -1;
    }

    if (startFunc == NULL) {
        return -1;
    }

    // Find a space in the PCB table for the new process being created
    PIDcounter++;
    int start_count = PIDcounter+1;
    while(PCB[PIDcounter % MAXPROC].pid != 0) {
        if(PIDcounter == start_count+MAXPROC) {
            // All slots are full
            return -1;
        }
        PIDcounter++;
    }
    
    // Create the new process and initialize all of its fields
    strcpy(PCB[PIDcounter % MAXPROC].name, name);
    PCB[PIDcounter % MAXPROC].arg = arg;

    PCB[PIDcounter % MAXPROC].pid = PIDcounter;
    PCB[PIDcounter % MAXPROC].startFunc = startFunc;
    PCB[PIDcounter % MAXPROC].stack = malloc(stackSize);
    PCB[PIDcounter % MAXPROC].stack_size = stackSize;
    PCB[PIDcounter % MAXPROC].priority = priority;
    PCB[PIDcounter % MAXPROC].status = 0;  // 0 for runnable/running
    USLOSS_ContextInit(&PCB[PIDcounter % MAXPROC].context, PCB[PIDcounter % MAXPROC].stack, PCB[PIDcounter % MAXPROC].stack_size, NULL, trampoline);
    PCB[PIDcounter % MAXPROC].my_parent = running_proc;
    PCB[PIDcounter % MAXPROC].first_child = NULL;
    PCB[PIDcounter % MAXPROC].next_sibling = NULL;
    PCB[PIDcounter % MAXPROC].prev_sibling = NULL;
    PCB[PIDcounter % MAXPROC].first_dead_child = NULL;
    PCB[PIDcounter % MAXPROC].next_dead_child = NULL;

   
    // Place the new process in the linked list of living children of the current running process
    if(running_proc->first_child == NULL) {
        running_proc->first_child = &PCB[PIDcounter % MAXPROC];
    }
    else {
        PCB[PIDcounter % MAXPROC].next_sibling = running_proc->first_child;
        running_proc->first_child->prev_sibling = &PCB[PIDcounter % MAXPROC];
        running_proc->first_child = &PCB[PIDcounter % MAXPROC];
    }

    // Restore interrupts
    USLOSS_PsrSet(oldPSR);

    // Return to the function that called spork with the pid of the child
    return PCB[PIDcounter % MAXPROC].pid;
}

/**************
* Function: join
* Parameters: int *status
* Returns: integer
* Description: This function is responsible for checking the status of the dead child or children of the current running
*              process. It will grab the status and pass it to the pointer to status that was passed as a parameter of
*              the function and then it will remove the child process from both the list of dead children of the current
*              running process and then the PCB table. Once both are done the function returns the pid of the process that
*              was just removed.
***************/
int join(int *status) {
    // Disable interrupts
    int oldPSR = disableInterrupts();

    // Error checking
    if (status == NULL) {
        return -3;
    }
    // No children to join
    if(running_proc->first_child == NULL && running_proc->first_dead_child == NULL) {
        return -2;
    }
    /* Block process if it has living children but no dead children - PHASE 1B
     if(running_proc->first_dead_child == NULL && running_proc->first_child != NULL) {
        blockMe()
    // }*/


    // Find the first child in the list of dead children of the current process and grab its status
    int dead_child_pid = running_proc->first_dead_child->pid;
    *status = running_proc->first_dead_child->status;

    // Remove the child from the linked list of dead children
    running_proc->first_dead_child = PCB[dead_child_pid % MAXPROC].next_dead_child;  // correct so fat
    
    // Remove the the dead child from the PCB table
    memset(&PCB[dead_child_pid % MAXPROC], 0, sizeof(struct process));
    
    // Restore interrupts
    USLOSS_PsrSet(oldPSR);

    // Return the PID of the child that joined to the current process
    return dead_child_pid;
}

/**************
* Function: quit_phase_1a
* Parameters: int status, int switchToPid
* Returns: void
* Description: This function is only used for part A of phase 1 and it is responsible for taking the status and pid that it
*              was given to change the status field of the current process to the status that was passed in and context
*              switch to the pid that was passed to it. It will also remove the process from the list of living children of
*              its parent and it will place the process at the head of the dead children list of the parent.
***************/
void quit_phase_1a(int status, int switchToPid) {
    // Check if in kernel mode
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call quit_phase_1a while in user mode!\n");
        USLOSS_Halt(1);
    }

    // Disable interrupts
    int oldPSR = disableInterrupts();

    // Error checking
    if (running_proc->first_child != NULL || running_proc->first_dead_child != NULL) {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", running_proc->pid);
        USLOSS_Halt(1);
    }
    
    // Change the status of the current process to the status that was passed in
    running_proc->status = status;
    
    // Grab sibling process to replace the current running process as first child of parent
    struct process *hold = running_proc->next_sibling;

    // Add the running process to the list of dead children
    if(running_proc->my_parent->first_dead_child == NULL) {
        running_proc->my_parent->first_dead_child = running_proc;   
    }
    else {
        running_proc->next_dead_child = running_proc->my_parent->first_dead_child;
        running_proc->my_parent->first_dead_child = running_proc;
    }

    // Remove the running process from the living children list; replace with sibling if it exists
    if(running_proc->prev_sibling == NULL) {
        if (hold == NULL) {
            running_proc->my_parent->first_child = NULL;
        }
        else {
            running_proc->my_parent->first_child = hold;
            hold->prev_sibling = NULL;
        }
    } 
    else {
        if (hold == NULL) {
            running_proc->prev_sibling->next_sibling = NULL;
        }
        else {
            hold->prev_sibling = running_proc->prev_sibling;
            hold->prev_sibling->next_sibling = hold;
            running_proc->prev_sibling = NULL;
        }
    }
    running_proc->next_sibling = NULL;

    // Clear the context of the current process
    memset(&running_proc->context, 0, sizeof(USLOSS_Context));

    // Restore interrupts
    USLOSS_PsrSet(oldPSR);

    TEMP_switchTo(switchToPid); 
}


/**************
* Function: dumpProcesses
* Parameters: void
* Returns: void
* Description: This function will print out information about the current processes in the PCB table.
* Resource: https://stackoverflow.com/questions/1809399/how-to-format-strings-using-printf-to-get-equal-length-in-the-output
***************/
void dumpProcesses(void) {
    // Disable interrupts
    int oldPSR = disableInterrupts();

    // Print out information about the current process
    printf(" PID  PPID  NAME              PRIORITY  STATE\n");

    int procNum = 0;
    while(procNum < MAXPROC) {
        if (PCB[procNum % MAXPROC].priority == 0) {
            procNum++;
            continue;
        }

        if (strcmp(PCB[procNum % MAXPROC].name, "init") == 0) {
            printf(" %-4d %-5d %-17s %-8d %s\n", 1, 0, "init", 6, " Runnable");
            procNum++;
            continue;
        }
        char *tempName = PCB[procNum].name;
        int tempPid = PCB[procNum].pid;
        int tempPPid = PCB[procNum].my_parent->pid;
        int tempPriority = PCB[procNum].priority;
        int tempStatus = PCB[procNum].status;

        if (running_proc == &PCB[procNum]) {
            printf(" %-4d %-5d %-17s %-8d %s\n", tempPid, tempPPid, tempName, tempPriority, " Running");
        }
        else if (tempStatus != 0) {
            printf(" %-4d %-5d %-17s %-8d %s(%d)\n", tempPid, tempPPid, tempName, tempPriority, " Terminated", tempStatus);
        }
        else {
            printf(" %-4d %-5d %-17s %-8d %s\n", tempPid, tempPPid, tempName, tempPriority, " Runnable");
        }
        procNum++;
    }

    // Restore interrupts
    USLOSS_PsrSet(oldPSR);
}

/**************
* Function: getpid
* Parameters: void
* Returns: integer
* Description: This function will take the running_proc global process and will get and return the pid of the current
*              running process.
***************/
int getpid(void) {
    return running_proc->pid;
}


/**************
* Function: TEMP_switchTo
* Parameters: int pid
* Returns: void
* Description: This function is responsible for context switching from the current running process to the process with
*              The pid value passed to the function as a parameter. It will save the context of the current process
*              before switching to the new one so that it can be restored when we context switch back.
***************/
void TEMP_switchTo(int pid) {
    struct process *oldProc = running_proc;
    struct process *newProc = &PCB[pid % MAXPROC];

    if (running_proc == NULL) {
        running_proc = newProc;
        USLOSS_ContextSwitch(NULL, &newProc->context);
    }

    else {
        running_proc = newProc;
        USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
    }
}

/**************
* Function: init
* Parameters: void
* Returns: void
* Description: This function is the first process that gets added to the PCB and it will call each of the other phases
*              to see if they have any processes that they would like to add to the PCB before init itself calls spork
*              to add the process testcase_main. It will then continue to loop and call join to see if it still has
*              children and will end once there are no more processes in the table. Once all the children have died and
*              join returns to say that there are no children left the process will hault the program.
***************/
void init() {
    // Disable interrupts
    int oldPSR = disableInterrupts();

    // Check other phases for processes that need to be added
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    // Create the process testcase_main
    int child_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using spork() to create it.\n");
    TEMP_switchTo(child_pid);

    int status, kidpid;

    // Loop until all children have been joined to
    while(1) {
        kidpid = join(&status);
        if(kidpid == -3 || kidpid == -2){
            USLOSS_Halt(1);
        }
    }

    // Restore interrupts
    USLOSS_PsrSet(oldPSR);
}

/**************
* Function: trampline
* Parameters: void
* Returns: void
* Description: This function is a "wrapper" function that isolates the current running process's start function and 
*              the associated argument so that they can be passed to USLOSS_ContextInit. 
***************/

void trampoline() {
    // Set the current mode to user mode and allow interrupts
    USLOSS_PsrSet(USLOSS_PSR_CURRENT_MODE | USLOSS_PSR_CURRENT_INT);
    
    retval = running_proc->startFunc(running_proc->arg);
    printf("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
    USLOSS_Halt(0);

}

/**************
* Function: disableInterrupts
* Parameters: void
* Returns: integer
* Description: This function is responsible for disabling interrupts so that the current process's functions can 
*              run without being interrupted by other processes. It will return the old PSR value so that it can 
*              be restored after the function has finished running.
***************/
int disableInterrupts() {
    int oldPSR = USLOSS_PsrGet();
    int newPSR = oldPSR & (~USLOSS_PSR_CURRENT_INT);  // bit manipulation
    USLOSS_PsrSet(newPSR);
    return oldPSR;
}