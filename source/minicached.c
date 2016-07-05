#include "minicached.h"
#include "hash.h"
#include "slabs.h"

#include <signal.h>
#include <time.h>

extern RET_T mnc_items_init(void);
extern RET_T mnc_hash_init(void);

#define TM_SIG     SIGUSR2
static  time_t     realtimer_id;
volatile time_t    current_time;

/**
 * 只用此处调用更新时间，可以节约很多的系统调用 
 * CLOCK_MONOTONIC这种时钟更加稳定，不受系统时钟的影响 
 */
static void timerHandler( int sig, siginfo_t *si, void *uc )
{
    assert(realtimer_id == *(time_t*)(si->si_value.sival_ptr));

    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
        return;

    current_time = ts.tv_sec;

    //st_d_print("%lu", current_time); //开机以来的时间差
    return;
}

extern RET_T mnc_timer_init(void)
{
    struct sigaction sa;
    struct sigevent sev;
    struct itimerspec its;

    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = timerHandler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(TM_SIG, &sa, NULL) == -1)
    {
        st_d_error("Failed to setup signal handling!");
        return RET_NO;
    }

    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo  = TM_SIG;
    sev.sigev_value.sival_ptr = &realtimer_id;
    if (timer_create(CLOCK_REALTIME, &sev, &realtimer_id) == -1)
    {
        st_d_error("Failed to create timer!");
        return RET_NO;
    }

    its.it_interval.tv_sec = 1;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = 1;
    its.it_value.tv_nsec = 0;
    timer_settime(realtimer_id, 0, &its, NULL); 

    return RET_YES;
}

extern RET_T mnc_init()
{
    if(mnc_items_init() == RET_NO)
        exit(EXIT_FAILURE);

    if(mnc_hash_init() == RET_NO)
        exit(EXIT_FAILURE);

    if(mnc_timer_init() == RET_NO)
        exit(EXIT_FAILURE);

    if (mnc_slab_init() == RET_NO)
        exit(EXIT_FAILURE);

    if (mnc_lru_init() == RET_NO)
        exit(EXIT_FAILURE);

    st_d_print("INITIALIZED OK!");

    return RET_YES;
}

