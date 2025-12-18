/*
 * Check if kernel aborts on illegal stacksize given to spork().
 */

#include <stdio.h>
#include <usloss.h>
#include <phase1.h>

int XXp1(void *), XXp2(void *);

int   tm_pid = -1;

int testcase_main()
{
    int pid1, status;

    tm_pid = getpid();

    USLOSS_Console("testcase_main(): started\n");
    USLOSS_Console("EXPECTATION: testcase_main() creates a child XXp2().  That process will try (many times) to create a child XXp1() with a too-small stack space.  Each call should fail.\n");

    USLOSS_Console("spork creating a child -- it will run next\n");
    pid1 = spork("XXp2", XXp2, "XXp2", 20 * (USLOSS_MIN_STACK - 10), 2);
    USLOSS_Console("Phase 1A TEMPORARY HACK: Manually switching to XXp2()\n");
    TEMP_switchTo(pid1);
    USLOSS_Console("testcase_main(): created XXp2 with pid = %d\n", pid1);

    USLOSS_Console("testcase_main(): calling join\n");
    pid1 = join( &status );
    USLOSS_Console("testcase_main(): join returned pid = %d, status = %d\n", pid1, status);

    return 0;
}

int XXp1(void *arg)
{
    USLOSS_Console("*** THIS SHOULD NEVER RUN ***\n");

    quit_phase_1a(2, -1);
}

int XXp2(void *arg)
{
    int i, pid1;

    for (i=0; i<4; i++)
    {
        pid1 = spork("XXp1", XXp1, "XXp1", USLOSS_MIN_STACK - 10, 2);
        if(pid1 == -2)
            USLOSS_Console("-2 returned, which is correct!\n");
        else
            USLOSS_Console("Wrong return value from spork\n");
    }

#if 0   /* REMOVED.  This is part of Phase 1b, but I'm removing it from 1a */
    return(0);
#else
    quit_phase_1a(0, tm_pid);
#endif
}

