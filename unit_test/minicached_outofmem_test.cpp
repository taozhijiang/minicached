#include "gtest/gtest.h"

#include "minicached.h"
#include "hash_lru.h"

RET_T mnc_outof_memory_test(void);
RET_T mnc_recycle_memory_test(void);


RET_T mnc_out_of_memory_test(void)
{
    // 2M -> 4*512K
    unsigned int i = 0;
    int key = 0x1;
    mnc_item* it = NULL;
    char* msg_fmt = "[%s]{%ld} TEST INFO....";
    char msg[512];

    st_d_print("!!! STAGE-I !!!");
    key = 0x1;
    for (i=0; i<6; i++)
    {
        it = mnc_new_item(&key, sizeof(int), 0, 512*1024-sizeof(mnc_item)-sizeof(int)-1);
        if (!it)
            break;

        sprintf(msg, msg_fmt, __FUNCTION__, i);
        mnc_store_item_l(&it, msg, strlen(msg) + 1);
        //mnc_class_statistic(it->slabs_clsid); 

        ++key;
    }

    st_d_print("!!! STAGE-II !!!");
    key = 0x1;
    for (i=0; i<6; i++)
    {
        it = mnc_get_item_l(&key, sizeof(int));
        if (!it)
        {
            ++key;
            continue;
        }

        //mnc_class_statistic(it->slabs_clsid); 
        mnc_unlink_item_l(it); 
        st_d_print("[%d]%s", (*(int *)ITEM_key(it)), (char *)ITEM_dat(it));
        mnc_remove_item(it);

        ++key;
    }

    st_d_print("!!! STAGE-III !!!");
    for (i=0; i<6; i++)
    {
        it = mnc_new_item(&key, sizeof(int), 0, 512*1024-sizeof(mnc_item)-sizeof(int)-1);
        if (!it)
            break;

        sprintf(msg, msg_fmt, __FUNCTION__, i);
        mnc_store_item_l(&it, msg, strlen(msg) + 1);
        //mnc_class_statistic(it->slabs_clsid); 

        ++key;
    }

    return RET_YES;
}

TEST(mnc_outofmem_test, mnc_out_of_memory_test) 
{
    EXPECT_EQ(RET_YES, mnc_out_of_memory_test());
}
