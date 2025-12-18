/* this test is a variation of test case 2 */

#include <stdio.h>
#include <usloss.h>
#include <phase1.h>

int XXp1(void *), XXp2(void *);

int   tm_pid  = -1;
int xxp1_pid  = -1;
int xxp2_pid1 = -1;
int xxp2_pid2 = -1;

int testcase_main()
{
    int status, pid1, kidpid;

    tm_pid = getpid();

    USLOSS_Console("testcase_main(): started\n");
    USLOSS_Console("EXPECTATION: Create XXp1 (moderate priority); that creates 2 XXp2 children (low priority).  XXp1 will block on join(), which will cause main() to wake up and report the spork() results - after which the XXp2 processes will run.\n");

    xxp1_pid = pid1 = spork("XXp1", XXp1, "XXp1", USLOSS_MIN_STACK, 2);
    USLOSS_Console("Phase 1A TEMPORARY HACK: Manually switching to XXp1()\n");
    TEMP_switchTo(pid1);
    USLOSS_Console("testcase_main(): after spork of child %d -- you should not see this until after XXp1() has created both of its children, and started its first join().  However, you should see it *before* either of the children run.\n", pid1);

    USLOSS_Console("testcase_main(): performing join\n");
    USLOSS_Console("Phase 1A TEMPORARY HACK: Manually switching to the first XXp2()\n");
    TEMP_switchTo(xxp2_pid1);
    kidpid = join(&status);
    USLOSS_Console("testcase_main(): exit status for child %d is %d\n", kidpid, status); 

    return 0;
}

int XXp1(void *arg)
{
    int kidpid;
    int status;

    xxp1_pid = getpid();

    USLOSS_Console("XXp1(): started\n");
    USLOSS_Console("XXp1(): arg = '%s'\n", arg);

    USLOSS_Console("XXp1(): executing spork of first child\n");
    xxp2_pid1 = kidpid = spork("XXp2", XXp2, "XXp2", USLOSS_MIN_STACK, 5);
    USLOSS_Console("XXp1(): spork of first child returned pid = %d\n", kidpid);

    USLOSS_Console("XXp1(): executing spork of second child\n");
    xxp2_pid2 = kidpid = spork("XXp2", XXp2, "XXp2", USLOSS_MIN_STACK, 5);
    USLOSS_Console("XXp1(): spork of second child returned pid = %d\n", kidpid);

    USLOSS_Console("XXp1(): performing first join\n");
    USLOSS_Console("Phase 1A TEMPORARY HACK: Manually switching to testcase_main(), since XXp1 has created its two children, which are lower priority than \n");
    TEMP_switchTo(tm_pid);
    kidpid = join(&status);
    USLOSS_Console("XXp1(): first join returned kidpid = %d, status = %d -- you should see this after the first child terminates, and before the second child even starts.\n", kidpid, status);

    USLOSS_Console("XXp1(): performing second join\n");
    USLOSS_Console("Phase 1A TEMPORARY HACK: Manually switching to the second XXp2()\n");
    TEMP_switchTo(xxp2_pid2);
    kidpid = join(&status);
    USLOSS_Console("XXp1(): second join returned kidpid = %d, status = %d\n", kidpid, status);

    quit_phase_1a(3, tm_pid);
}

int XXp2(void *arg)
{
    USLOSS_Console("XXp2(): started -- you should not see this until both XXp1() and testcase_main() are blocked in join().\n");
    USLOSS_Console("XXp2(): arg = '%s'\n", arg);

    USLOSS_Console("XXp2(): This process will terminate immediately.\n");
    quit_phase_1a(5, xxp1_pid);
}

