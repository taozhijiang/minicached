#include "minicached.h"
#include "hash.h"


extern RET_T mnc_items_init(void);
extern RET_T mnc_hash_init(void);

extern RET_T mnc_init()
{
    if(mnc_items_init() == RET_NO)
        exit(EXIT_FAILURE);

    if(mnc_hash_init() == RET_NO)
        exit(EXIT_FAILURE);

    st_d_print("INITIALIZED OK!");

    return RET_YES;
}

