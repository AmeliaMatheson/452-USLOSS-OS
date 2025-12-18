
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>

#include <stdio.h>
#include <assert.h>



void startup(int argc, char **argv)
{
    phase1_init();
    phase2_init();
    dispatcher();
}



/* force the testcase driver to priority 1, instead of the
 * normal priority for testcase_main
 */
int start2(void*);

int testcase_main()
{
    int pid_fork, pid_join;
    int status;

    pid_fork = spork("start2", start2, "start2", 4*USLOSS_MIN_STACK, 1);
    pid_join = join(&status);

    if (pid_join != pid_fork)
    {
        USLOSS_Console("*** TESTCASE FAILURE *** - the join() pid doesn't match the fork() pid.  %d/%d\n", pid_fork,pid_join);
        USLOSS_Halt(1);
    }

    return status;
}



USLOSS_PTE *phase5_mmu_pageTable_alloc(int pid)
{
    return NULL;
}

void phase5_mmu_pageTable_free(int pid, USLOSS_PTE *page_table)
{
    assert(page_table == NULL);
}



void phase3_start_service_processes()
{
    USLOSS_Console("%s() called -- currently a NOP\n", __func__);
}

void phase4_start_service_processes()
{
    USLOSS_Console("%s() called -- currently a NOP\n", __func__);
}

void phase5_start_service_processes()
{
    USLOSS_Console("%s() called -- currently a NOP\n", __func__);
}



void finish(int argc, char **argv)
{
    USLOSS_Console("%s(): The simulation is now terminating.\n", __func__);
}

void test_setup  (int argc, char **argv) {}
void test_cleanup(int argc, char **argv) {}

