#include "gtest/gtest.h"

#include "minicached.h"
#include "hash_lru.h"

extern struct mnc_stat mnc_status;

RET_T mnc_expire_test(void);
RET_T mnc_lru_trim_test(void);

TEST(minicached_expire_lru, minicached_expire_lru) 
{
    EXPECT_EQ(RET_YES, mnc_expire_test());
    EXPECT_EQ(RET_YES, mnc_lru_trim_test());
}

// 申请内存时候expire回收
RET_T mnc_expire_test(void)
{
    // 2M -> 4*512K
    unsigned int i = 0;
    int key = 0x1;
    mnc_item* it = NULL;
    char* msg_fmt = "[%s]{%ld} TEST INFO....";
    char msg[512];

    // 消耗完内存
    key = 0x1;
    for (i=0; i<4; i++)
    {
        it = mnc_new_item(&key, sizeof(int), 3, 512*1024-sizeof(mnc_item)-sizeof(int)-1);
        if (!it)
            break;

        sprintf(msg, msg_fmt, __FUNCTION__, i);
        mnc_store_item_l(&it, msg, strlen(msg) + 1);

        ++key;
    }

    mnc_sleep(1);
    st_d_print("TIME:%lu", mnc_status.current_time);
    mnc_sleep(3);
    st_d_print("TIME:%lu", mnc_status.current_time);

    // 新插入元素，expired回收替换
    key = 0x10;
    for (i=0; i<4; i++)
    {
        it = mnc_new_item(&key, sizeof(int), 3, 512*1024-sizeof(mnc_item)-sizeof(int)-1);

        if (!it)
            goto failed;

        sprintf(msg, msg_fmt, __FUNCTION__, i+0x99);
        mnc_store_item_l(&it, msg, strlen(msg) + 1);

        ++key;
    }

    key = 0x10;
    for (i=0; i<4; i++)
    {
        it = mnc_get_item_l(&key, sizeof(int));
        if (!it)
        {
            ++key;
            continue;
        }

        mnc_unlink_item_l(it); 
        st_d_print("[%x]%s", (*(int *)ITEM_key(it)), (char *)ITEM_dat(it));
        mnc_remove_item(it);

        ++key;
    }

    mnc_class_statistic(mnc_slabs_clsid(512*1024-sizeof(mnc_item)-sizeof(int)-1)); 

    st_d_print("DONE!");
    return RET_YES;

failed:
    return RET_NO;

}


RET_T mnc_lru_trim_test(void)
{
    unsigned int i = 0;
    int key = 0x1;
    mnc_item* it = NULL;
    mnc_item* it2 = NULL;
    char* msg_fmt = "%d";
    char msg[512];

    st_d_print("STAGE-I");
    key = 0x1;
    for (i=0; i<4; i++)
    {
        it = mnc_new_item(&key, sizeof(int), 0, 512*1024-sizeof(mnc_item)-sizeof(int)-1);
        if (!it)
            goto failed;

        sprintf(msg, msg_fmt, i+10);  // 10 11 12 13
        mnc_store_item_l(&it, msg, strlen(msg) + 1);

        ++key;
    }

    // LRU 淘汰
    st_d_print("STAGE-II");
    key = 0x10;
    for (i=0; i<8; i++)
    {
        it = mnc_new_item(&key, sizeof(int), 0, 512*1024-sizeof(mnc_item)-sizeof(int)-1);
        if (!it)
            goto failed;

        sprintf(msg, msg_fmt, i+20);    // 20 21 22 23
        mnc_store_item_l(&it, msg, strlen(msg) + 1);

        int tmp_key;
        tmp_key = key;
        it2 = mnc_get_item_l(&tmp_key, sizeof(int));
        st_d_print("ADDR:%p [%d]%s", it2, (*(int *)ITEM_key(it2)), (char *)ITEM_dat(it2));

        ++key;
    }

    st_d_print("STAGE-III");
    //0x10 11 12 13  14 15 16 17
    key = 0x50;
    it = mnc_new_item(&key, sizeof(int), 0, 512*1024-sizeof(mnc_item)-sizeof(int)-1);
    // acutually, not linked here!

    key = 0x15;
    assert(mnc_get_item_l(&key, sizeof(int)));
    key = 0x16;
    assert(mnc_get_item_l(&key, sizeof(int)));
    key = 0x17;
    assert(mnc_get_item_l(&key, sizeof(int)));

    mnc_class_statistic(mnc_slabs_clsid(512*1024-sizeof(mnc_item)-sizeof(int)-1)); 

    mnc_mem_cleanup();
    st_d_print("DONE!");

    return RET_YES;

failed:
    return RET_NO;
}

