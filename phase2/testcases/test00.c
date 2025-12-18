
/* Easy getting started test.
 * start2 prints a message and quits.
 */

#include <phase1.h>
#include <phase2.h>



int start2(void *arg)
{
    USLOSS_Console("start2(): started\n");

    USLOSS_Console("start2(): terminating\n");
    quit(0);
}

