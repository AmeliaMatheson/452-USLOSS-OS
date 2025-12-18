#include <phase1.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <usloss.h>
#include <string.h>
#include <time.h>

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
    int block;
    int joinBlock;
    int zapBlock;
    USLOSS_Context context;
    struct process *run_queue_next;
    struct process *my_parent;
    struct process *first_child;
    struct process *next_sibling;
    struct process *prev_sibling;
    struct process *first_dead_child;
    struct process *next_dead_child;
    struct process *zapping_proc;
    struct process *zappers;
};

// These are the global variables for this phase
struct process PCB[MAXPROC];
struct process *running_proc;
int PIDcounter = 1;
struct process *pri1Q;
struct process *pri2Q;
struct process *pri3Q;
struct process *pri4Q;
struct process *pri5Q;
struct process *pri6Q;
int lastSwitch;


// These are the function prototypes for this phase
void phase1_init(void);
int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority);
int join(int *status);
void quit(int status);
void dumpProcesses(void);
void trampoline();
int disableInterrupts();
void enqueue(struct process **head, struct process *new_proc);
void dequeue(struct process **head, struct process *proc);
void dispatcher();
void blockMe();
int unblockProc(int pid);
void zap(int pid);
int getpid(void);
void init();



/**************
* Function: phase1_init
* Parameters: void
* Returns: void
* Description: This function will initialize our array called PCB and then will insert our first process called init
*              into the PCB table anf initialize all of its fields
***************/
void phase1_init(void) {
    // disable interrupts
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
    PCB[slotNum].status = 0;  // 0 for runnable/running

    // place into pri6Q
    pri6Q = &PCB[slotNum];

    lastSwitch = 0;
    pri1Q = NULL;
    pri2Q = NULL;
    pri3Q = NULL;
    pri4Q = NULL;
    pri5Q = NULL;

    // set the running process to NULL
    running_proc = NULL;

    // restore interrupts
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

    // find a space in the PCB table for the new process being created
    PIDcounter++;
    int start_count = PIDcounter;
    while(PCB[PIDcounter % MAXPROC].priority != 0) {
        if(PIDcounter == start_count+MAXPROC) {
            // All slots are full and this is an error
            return -1;
        }
        PIDcounter++;
    }

    // Create the new process and initialize all of its fields
    strcpy(PCB[PIDcounter % MAXPROC].name, name);
    PCB[PIDcounter % MAXPROC].arg = arg;

    // create the new process and initialize all of its fields
    PCB[PIDcounter % MAXPROC].pid = PIDcounter;
    PCB[PIDcounter % MAXPROC].startFunc = startFunc;
    PCB[PIDcounter % MAXPROC].stack = malloc(stackSize);
    PCB[PIDcounter % MAXPROC].stack_size = stackSize;
    PCB[PIDcounter % MAXPROC].priority = priority;
    PCB[PIDcounter % MAXPROC].status = 0;  // 0 for runnable/running
    PCB[PIDcounter % MAXPROC].block = 0;
    PCB[PIDcounter % MAXPROC].joinBlock = 0;
    PCB[PIDcounter % MAXPROC].zapBlock = 0;
    USLOSS_ContextInit(&PCB[PIDcounter % MAXPROC].context, PCB[PIDcounter % MAXPROC].stack, PCB[PIDcounter % MAXPROC].stack_size, NULL, trampoline);
    PCB[PIDcounter % MAXPROC].run_queue_next = NULL;
    PCB[PIDcounter % MAXPROC].my_parent = running_proc;
    PCB[PIDcounter % MAXPROC].first_child = NULL;
    PCB[PIDcounter % MAXPROC].next_sibling = NULL;
    PCB[PIDcounter % MAXPROC].prev_sibling = NULL;
    PCB[PIDcounter % MAXPROC].first_dead_child = NULL;
    PCB[PIDcounter % MAXPROC].next_dead_child = NULL;
    PCB[PIDcounter % MAXPROC].zapping_proc = NULL;
    PCB[PIDcounter % MAXPROC].zappers = NULL;

    
    // place the new process in the linked list of living children of the current running process
    if(running_proc->first_child == NULL) {
        running_proc->first_child = &PCB[PIDcounter % MAXPROC];
    }
    else {
        PCB[PIDcounter % MAXPROC].next_sibling = running_proc->first_child;
        running_proc->first_child->prev_sibling = &PCB[PIDcounter % MAXPROC];
        running_proc->first_child = &PCB[PIDcounter % MAXPROC];
    }

    int kid_pid = PCB[PIDcounter % MAXPROC].pid;

    // Find appropriate queue to place the new process in
    newEnqueue(&PCB[PIDcounter % MAXPROC], priority);

    // Call the dispatcher
    dispatcher();

    // restore interrupts
    USLOSS_PsrSet(oldPSR);

    // return to the function that called spork with an error or the pid of the child
    return kid_pid;  // this will be 0 if we call join after 
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

    if (status == NULL) {
        return -3;
    }

    // No children to join
    if(running_proc->first_child == NULL && running_proc->first_dead_child == NULL) {
        return -2;
    }

    // Block process if it has living children but no dead children - PHASE 1B
    if(running_proc->first_child != NULL && running_proc->first_dead_child == NULL) {
        running_proc->joinBlock = 1;
        blockMe();
    }

    int dead_child_pid = 0;
    
    // run this section when we come back to this process after someone unblocks me
    dead_child_pid = running_proc->first_dead_child->pid;

    // find the first child in the list of dead children of the current process and grab its status
    *status = running_proc->first_dead_child->status;

    // remove the child from the linked list of dead children
    running_proc->first_dead_child = PCB[dead_child_pid % MAXPROC].next_dead_child;  // correct so fat
    
    // remove the the dead child from the PCB table
    memset(&PCB[dead_child_pid % MAXPROC], 0, sizeof(struct process));  // MARKING, should prob be dead_child_pid % MAXPROC
    
    
    // restore interrupts
    USLOSS_PsrSet(oldPSR);

    // return the PID of the child that joined to the current process
    return dead_child_pid;
}

void quit(int status) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call quit while in user mode!\n");
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

    // add the running process to the list of dead children
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

    // Find appropriate queue to remove the process from
    newDequeue(running_proc, running_proc->priority);
    
    
    // Wake up my parent if it was blocked on join
    if (running_proc->my_parent->joinBlock == 1) {
        running_proc->my_parent->joinBlock = 0;
        running_proc->my_parent->block = 0;

        // add the parent back to its prioriority queue but don't call dispatcher yet; need to clear zappers first.
        newEnqueue(running_proc->my_parent, running_proc->my_parent->priority);
    }

    // tell processes that zapped me that I am terminated and set their zapping_proc field to NULL
    // empty my zap list (we can get the head of the list, update its field in that process and then dequeue)
    if (running_proc->zappers != NULL) {
        struct process *curr = running_proc->zappers;
        while (curr != NULL) {
            curr->zapping_proc = NULL;  // remove running_proc from curr's zapping_proc field
            curr->zapBlock = 0;  // unblock the process that zapped me
            curr->block = 0; 
            dequeue(&running_proc->zappers, curr);

            // put curr back on its appropriate queue
            newEnqueue(curr, curr->priority);

            curr = curr->run_queue_next;
        }
    }

    // now call dispatcher since everyone that needs to be woken up is woken up
    dispatcher();

    // restore interrupts
    USLOSS_PsrSet(oldPSR);

    // Choose next process to run
    dispatcher();
}


void zap(int pid) {

    if (running_proc->pid == pid) {
        USLOSS_Console("ERROR: Attempt to zap() itself.\n");
        USLOSS_Halt(1);
    }
    if (pid == 1) {
        USLOSS_Console("ERROR: Attempt to zap() init.\n");
        USLOSS_Halt(1);
    }
    if (PCB[pid % MAXPROC].pid == 0 || pid > PIDcounter) {
        USLOSS_Console("ERROR: Attempt to zap() a non-existent process.\n");
        USLOSS_Halt(1);
    }

    if (PCB[pid % MAXPROC].status != 0) {
        USLOSS_Console("ERROR: Attempt to zap() a process that is already in the process of dying.\n");
        USLOSS_Halt(1);
    }

    // add running_proc to list of processes zapping the target process
    struct process *zapped = &PCB[pid%MAXPROC];
    running_proc->zapping_proc = zapped;

    // add running_proc to the zap list of the target process
    if (zapped->zappers == NULL) {
        zapped->zappers = running_proc;
    }
    else {
        enqueue(&zapped->zappers, running_proc);
    }

    // Block me until zap target is terminated
    running_proc->zapBlock = 1;
    blockMe();
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
* Function: dumpProcesses
* Parameters: void
* Returns: void
* Description: This function will print out information about the current processes in the PCB table.
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
        else if (PCB[procNum].block == 1) {
            if (PCB[procNum].joinBlock == 1) {
                printf(" %-4d %-5d %-17s %-8d %s\n", tempPid, tempPPid, tempName, tempPriority, " Blocked(waiting for child to quit)");
            }
            else if (PCB[procNum].zapBlock == 1) {
                printf(" %-4d %-5d %-17s %-8d %s\n", tempPid, tempPPid, tempName, tempPriority, " Blocked(waiting for zap target to quit)");
            }
            else {
                printf(" %-4d %-5d %-17s %-8d %s\n", tempPid, tempPPid, tempName, tempPriority, " Blocked(3)");
            }
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
* Function: blockMe
* Parameters: void
* Returns: void
* Description: This function will set the current running process to be blocked and then will take the process off of the
*              priority queue run list. It will then call the dispatcher to switch to a new process.
***************/
void blockMe() {
    running_proc->block = 1;

    // Take off of the priority queue run list
    newDequeue(running_proc, running_proc->priority);

    dispatcher();
}

/**************
* Function: unblockProc
* Parameters: int pid
* Returns: integer
* Description: This function will take a pid and will find the process in the PCB table that has that pid. It will then
*              check if the process is blocked and if it is it will unblock the process and put it back on the priority
*              queue run list. It will then call the dispatcher to switch to the newly unblocked process.
***************/
int unblockProc(int pid) {
    struct process *temp = &PCB[pid%MAXPROC];
    if (running_proc->pid == pid || temp->block == 0) {
        return -2;
    }

    temp->block = 0;

    // put on the priority queue run list
    newEnqueue(temp, temp->priority);

    dispatcher();

    return 0;
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
    // disable interrupts
    int oldPSR = disableInterrupts();

    // Check other phases for processes that need to be added
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    // Create the process testcase_main
    int child_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);

    int status, kidpid;

    // loop until all children have been joined to
    while(1) {
        kidpid = join(&status);
        if(status == -3 || status == -2){
            USLOSS_Halt(1);
        }
    }

    // restore interrupts
    USLOSS_PsrSet(oldPSR);
}

void trampoline() {
    // Set the current mode to user mode and allow interrupts
    USLOSS_PsrSet(USLOSS_PSR_CURRENT_MODE | USLOSS_PSR_CURRENT_INT);

    int retval = running_proc->startFunc(running_proc->arg);
    if (strcmp(running_proc->name, "testcase_main") != 0) {
        // call quit if anything other than testcase_main returns
        quit(retval);
    }

    USLOSS_Halt(retval);
}


/**************
* Function: dispatcher
* Parameters: void
* Returns: void
* Description: This function is responsible for switching between processes in the PCB table. It will check the current
*              running process and then check the priority of the process to see if it is time to switch to a new process.
*              If it is time to switch to a new process it will call USLOSS_ContextSwitch to switch to the new process.
***************/
void dispatcher() {
    // disable interrupts
    int oldPSR = disableInterrupts();

    // dispatcher is called immediately after phase1_init (running_proc is NULL)
    if (running_proc == NULL) {
        running_proc = pri6Q;
        lastSwitch = currentTime();
        USLOSS_ContextSwitch(NULL, &running_proc->context);
    }
    int currentPriority = running_proc->priority;    

    struct process *oldProc = running_proc;
    struct process *newProc;
    
    if (pri1Q != NULL) {
        if (currentPriority == 1) {
            if (currentTime() - lastSwitch >= 80) {
                // put running_proc back on the queue
                newDequeue(running_proc, running_proc->priority);
                newEnqueue(running_proc, running_proc->priority);
                newProc = pri1Q;
                running_proc = newProc;
                lastSwitch = currentTime();
                USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
            }
        }
        // run processes from here
        newProc = pri1Q;
        running_proc = newProc;
        lastSwitch = currentTime();
        USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
    }
    else if (pri2Q != NULL) {
        if (currentPriority == 2) {
            if (currentTime() - lastSwitch >= 80) {
                // put running_proc back on the queue
                newDequeue(running_proc, running_proc->priority);
                newEnqueue(running_proc, running_proc->priority);
                newProc = pri2Q;
                running_proc = newProc;
                lastSwitch = currentTime();
                USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
            }
        }
        newProc = pri2Q;
        running_proc = newProc;
        lastSwitch = currentTime();
        USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
    }
    else if (pri3Q != NULL) {
        if (currentPriority == 3) {
            if (currentTime() - lastSwitch >= 80) {
                // put running_proc back on the queue
                newDequeue(running_proc, running_proc->priority);
                newEnqueue(running_proc, running_proc->priority);
                newProc = pri3Q;
                running_proc = newProc;
                lastSwitch = currentTime();
                USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
            }
        }
        // run processes from here
        newProc = pri3Q;
        running_proc = newProc;
        lastSwitch = currentTime();
        USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
    }
    else if (pri4Q != NULL) {
        if (currentPriority == 4) {
            if (currentTime() - lastSwitch >= 80) {
                // put running_proc back on the queue
                newDequeue(running_proc, running_proc->priority);
                newEnqueue(running_proc, running_proc->priority);
                newProc = pri4Q;
                running_proc = newProc;
                lastSwitch = currentTime();
                USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
            }
        }
        // run processes from here
        newProc = pri4Q;
        running_proc = newProc;
        lastSwitch = currentTime();
        USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
    }
    else if (pri5Q != NULL) {
        if (currentPriority == 5) {
            if (currentTime() - lastSwitch >= 80) {
                // put running_proc back on the queue
                newDequeue(running_proc, running_proc->priority);
                newEnqueue(running_proc, running_proc->priority);
                newProc = pri5Q;
                running_proc = newProc;
                lastSwitch = currentTime();
                USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
            }
        }
        // run processes from here
        newProc = pri5Q;
        running_proc = newProc;
        lastSwitch = currentTime();
        USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
    }
    else if (pri6Q != NULL) {
        if (currentPriority == 6) {
            if (currentTime() - lastSwitch >= 80) {
                // put running_proc back on the queue
                newDequeue(running_proc, running_proc->priority);
                newEnqueue(running_proc, running_proc->priority);
                newProc = pri6Q;
                running_proc = newProc;
                lastSwitch = currentTime();
                USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
            }
        }
        // run processes from here
        newProc = &pri6Q;
        running_proc = newProc;
        lastSwitch = currentTime();
        USLOSS_ContextSwitch(&oldProc->context, &newProc->context);
    }
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
    // disable interrupts with bit manipulation
    int oldPSR = USLOSS_PsrGet();
    int newPSR = oldPSR & (~USLOSS_PSR_CURRENT_INT);
    USLOSS_PsrSet(newPSR);
    return oldPSR;
}

/**************
* Function: enqueue
* Parameters: struct process **head, struct process *new_proc
* Returns: void
* Description: This function is responsible for adding a process to the end of the queue that it is in. It will take the
*              head of the queue and the process that needs to be added and then will add the process to the end of the
*              queue.
***************/
void enqueue(struct process **head, struct process *new_proc) {
    //printf("Enqueue called; adding %s to its queue\n", new_proc->name);
    if (*head == NULL) {
        *head = new_proc;  // marking
    } 
    else {
        struct process *curr = *head;
        while (curr->run_queue_next != NULL) {
            curr = curr->run_queue_next;
        }
        
        curr->run_queue_next = new_proc;
    }
}

/**************
* Function: dequeue
* Parameters: struct process **head, struct process *proc
* Returns: void
* Description: This function is responsible for removing a process from the queue that it is in. It will take the head
*              of the queue and the process that needs to be removed and then will remove the process from the queue.
***************/
void dequeue(struct process **head, struct process *proc) {
    //printf("Dequeue called; removing %s from its queue\n", proc->name);
    if (head == NULL || proc == NULL) {
        return;
    }

    if (head == proc) {
        head = proc->run_queue_next;
        proc->run_queue_next = NULL;
        return;
    }

    struct process *curr = head;
    while (curr->run_queue_next != NULL) {
        if (curr->run_queue_next == proc) {
            curr->run_queue_next = proc->run_queue_next;
            proc->run_queue_next = NULL;
            return;
        }
        curr = curr->run_queue_next;
    }
}

/**************
* Function: newEnqueue
* Parameters: struct process *proc, int priority
* Returns: void
* Description: This function is responsible for adding a process to the correct queue based on its priority level. It will
*              take the process and the priority level and then add the process to the end of the queue.
* Resource: https://www.geeksforgeeks.org/c-switch-statement/
***************/

void newEnqueue(struct process *proc, int priority) {
    struct process **queue = NULL;

    switch(priority) {
        case 1:
            queue = &pri1Q;
            break;
        case 2:
            queue = &pri2Q;
            break;
        case 3:
            queue = &pri3Q;
            break;
        case 4:
            queue = &pri4Q;
            break;
        case 5:
            queue = &pri5Q;
            break;
        case 6:
            queue = &pri6Q;
            break;
        default:
            break;
    }

    // add the process to the end of the queue
    if (*queue == NULL) {
        *queue = proc;
    } 
    else {
        struct process *curr = *queue;
        while (curr->run_queue_next != NULL) {
            curr = curr->run_queue_next;
        }
        curr->run_queue_next = proc;
    }
}

/**************
* Function: newDequeue
* Parameters: struct process *proc, int priority
* Returns: void
* Description: This function is responsible for removing a process from the correct queue based on its priority level. It will
*              take the process and the priority level and then remove the process from the queue.
* Resource: https://www.geeksforgeeks.org/c-switch-statement/
***************/
void newDequeue(struct process *proc, int priority) {
    struct process **queue = NULL;

    switch(priority) {
        case 1:
            queue = &pri1Q;
            break;
        case 2:
            queue = &pri2Q;
            break;
        case 3:
            queue = &pri3Q;
            break;
        case 4:
            queue = &pri4Q;
            break;
        case 5:
            queue = &pri5Q;
            break;
        case 6:
            queue = &pri6Q;
            break;
        default:
            break;
    }

    if (queue == NULL || proc == NULL) {
        return;
    }

    if (*queue == proc) {
        *queue = proc->run_queue_next;
        proc->run_queue_next = NULL;
        return;
    }

    struct process *curr = *queue;
    while (curr->run_queue_next != NULL) {
        if (curr->run_queue_next == proc) {
            curr->run_queue_next = proc->run_queue_next;
            proc->run_queue_next = NULL;
            return;
        }
        curr = curr->run_queue_next;
    }
}