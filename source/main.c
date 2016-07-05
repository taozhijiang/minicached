#include <stdio.h>
#include "minicached.h"

#include <signal.h>

int main(int argc, char* argv[])
{
#if 1
    // For debug with segment fault
    struct sigaction sa;
    sa.sa_handler = backtrace_info;
    sigaction(SIGSEGV, &sa, NULL);

    // ignore SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGABRT, SIG_IGN);

#endif

    mnc_init();

    while (1)
    {
        sleep(3);
    }
    return 0;
}
