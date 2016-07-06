#include <stdio.h>
#include "minicached.h"

#include <signal.h>

#include <json-c/json.h>
#include <json-c/json_tokener.h>

extern size_t mem_limit;

static RET_T load_settings(void)
{
    json_object *p_obj = NULL;
    json_object *p_class = NULL;
    json_object *p_store_obj = NULL;

    if( ! (p_obj = json_object_from_file("settings.json")) )
        return RET_NO;

    if(json_object_object_get_ex(p_obj,"settings",&p_class))
    {
        if (json_object_object_get_ex(p_class,"total_mem",&p_store_obj))
            mem_limit = json_object_get_int(p_store_obj) * 1024 * 1024; 
        else
            mem_limit = 64 * 1024 * 1024; //default 64M

        assert(mem_limit > 0);
    }


    // dump settings
    st_d_print("TOTAL MEMORY: %d", mem_limit);

    return RET_YES;
}

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

    load_settings();
    mnc_init();

    while (1)
    {
        sleep(3);
    }
    return 0;
}
