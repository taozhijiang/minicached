#include "gtest/gtest.h"

#include "minicached.h"
#include "hash_lru.h"

extern struct mnc_stat mnc_status;

RET_T mnc_rebalance_test(void);

TEST(mnc_rebalance_test, mnc_rebalance_test) 
{
    EXPECT_EQ(RET_YES, mnc_rebalance_test());
}

RET_T mnc_rebalance_test(void)
{
    // 2M -> 4*512K
    unsigned int i = 0;
    int key = 0x1;
    mnc_item* it = NULL;
    char* msg_fmt = "D->[%s]{%ld} TEST INFO....";
    char msg[512];
    unsigned int cls_id = 0;


    // 消耗完内存
    st_d_print("STAGE-I");
    key = 0x1;
    for (i=0; i<8; i++)
    {
        it = mnc_new_item(&key, sizeof(int), key%2?2:0, 256*1024-sizeof(mnc_item)-sizeof(int)-1);
        sprintf(msg, msg_fmt, __FUNCTION__, key);
        mnc_store_item_l(&it, msg, strlen(msg) + 1);

        ++key;
    }
    mnc_sleep(3);

    // 刷新产生内存碎片
    key = 0x1;
    for (i=0; i<8; i++)
    {
        it = mnc_get_item_l(&key, sizeof(int));
        ++key;
    }

    cls_id = mnc_slabs_clsid(256*1024-sizeof(mnc_item)-sizeof(int)-1);
    mnc_class_statistic(mnc_slabs_clsid(256*1024-sizeof(mnc_item)-sizeof(int)-1) );

    st_d_print("STAGE-II");
    key = 0x10;

    for (i=0; i<4; i++) // only 2 will be stored
    {
        it = mnc_new_item(&key, sizeof(int), 0, 512*1024-sizeof(mnc_item)-sizeof(int)-1);
        sprintf(msg, msg_fmt, __FUNCTION__, key|0x20);
        mnc_store_item_l(&it, msg, strlen(msg) + 1);

        ++key;
    }

    cls_id = mnc_slabs_clsid(256*1024-sizeof(mnc_item)-sizeof(int)-1);
    mnc_class_statistic(mnc_slabs_clsid(256*1024-sizeof(mnc_item)-sizeof(int)-1));
    cls_id = mnc_slabs_clsid(512*1024-sizeof(mnc_item)-sizeof(int)-1);
    mnc_class_statistic(mnc_slabs_clsid(512*1024-sizeof(mnc_item)-sizeof(int)-1));

    st_d_print("STAGE-III");
    key = 0x10;
    for (i=0; i<4; i++)
    {
        it = mnc_get_item_l(&key, sizeof(int));
        if (it)
            st_d_print("[%d]%s", (*(int *)ITEM_key(it)), (char *)ITEM_dat(it));

        ++key;
    }

    mnc_mem_cleanup();

    st_d_print("DONE!");
    return RET_YES;

failed:
    return RET_NO;

}

