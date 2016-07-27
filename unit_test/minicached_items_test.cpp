#include "gtest/gtest.h"

#include "minicached.h"
#include "hash_lru.h"

RET_T mnc_item_test_int_key(void);
RET_T mnc_item_test_char_key(void);
RET_T mnc_item_test_lru_touch(void);

TEST(mnc_item_test_int_key, int_key) 
{
    EXPECT_EQ(RET_YES, mnc_item_test_int_key());
}

RET_T mnc_item_test_int_key(void)
{
    int i_key = 0x12333;
    unsigned int key_len = sizeof(i_key);
    char* msg = "桃子的值还是桃子！";
    char* msg2 = "桃子的值还是桃子他大爷的啊！";
    mnc_item* it = NULL;
    mnc_item* g_it = NULL;
    mnc_item* g_it_old = NULL;


    it = mnc_new_item(&i_key, key_len, 2, strlen(msg)+1);
    mnc_link_item_l(it);
    mnc_store_item_l(&it, msg, strlen(msg)+1);
    g_it = mnc_get_item_l(&i_key, key_len);
    assert(g_it);
    st_d_print("VALUE1:%s", ITEM_dat(g_it)); 

    mnc_sleep(4);
    assert(!mnc_get_item_l(&i_key, key_len));
    st_d_print("good, expired item can not be got!");

    mnc_mem_cleanup();

    st_d_print("DONE!");

    return RET_YES;
}


TEST(mnc_item_test_char_key, char_key) 
{
    EXPECT_EQ(RET_YES, mnc_item_test_char_key());
}

RET_T mnc_item_test_char_key(void)
{
    char* key ="桃子";
    unsigned int key_len = strlen(key) + 1;
    char* msg = "桃子的值还是桃子！";
    char* msg2 = "桃子的值还是桃子他大爷的啊AAAAAAAAAAAAAA"
                 "AAAAAAAAAAAAAAAa桃子的值还是桃子他大爷的啊AAAAAAAAAAAAAAAAAAAAAAAAAAAAAa"
                 "桃子的值还是桃子他大爷的啊AAAAAAAAAAAAAAAAAAAAAAAAAAAAAa桃子的值还是桃子他大爷的啊AAA"
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAa！";
    mnc_item* it = NULL;
    mnc_item* g_it = NULL;
    mnc_item* g_it_old = NULL;


    it = mnc_new_item(key, key_len, 0, strlen(msg)+1);
    assert(!mnc_get_item_l(key, key_len));
    mnc_link_item_l(it);
    mnc_store_item_l(&it, msg, strlen(msg)+1);
    assert(mnc_get_item_l(key, key_len));
    st_d_print("VALUE1:%s", ITEM_dat(mnc_get_item_l(key, key_len))); 

    g_it = g_it_old = mnc_get_item_l(key, key_len); // should be reallocated
    mnc_store_item_l(&g_it, msg2, strlen(msg2)+1);
    assert(g_it);
    assert(g_it != g_it_old);
    st_d_print("VALUE2:%s", ITEM_dat(mnc_get_item_l(key, key_len))); 

    int k2 = 0x23353;
    it = mnc_new_item(&k2, sizeof(k2), 0, strlen(msg2)+1);
    assert(!mnc_get_item_l(&k2, sizeof(k2)));
    mnc_store_item_l(&it, msg2, strlen(msg2)+1); //auto store link
    assert(mnc_get_item_l(&k2, sizeof(k2)));


    g_it = mnc_get_item_l(&k2, sizeof(k2));
    mnc_unlink_item_l(g_it); 
    assert(!mnc_get_item_l(&k2, sizeof(k2)));
    mnc_remove_item(g_it);

    g_it = mnc_get_item_l(key, key_len);
    mnc_unlink_item_l(g_it); 
    mnc_remove_item(g_it);
    assert(!mnc_get_item_l(key, key_len));

    mnc_mem_cleanup();

    st_d_print("DONE!");

    return RET_YES;
}

TEST(mnc_item_test_lru_touch, lru_touch_time) 
{
    EXPECT_EQ(RET_YES, mnc_item_test_lru_touch());
}

RET_T mnc_item_test_lru_touch(void)
{
    int i_key = 0x12333;
    unsigned int key_len = sizeof(i_key);
    char* msg = "桃子的值还是桃子！";

    mnc_item* it = NULL;

    it = mnc_new_item(&i_key, key_len, 0, strlen(msg)+1);
    mnc_store_item_l(&it, msg, strlen(msg)+1);
    
    time_t t1 = it->time;
    mnc_sleep(1);
    mnc_update_item(it, false);
    assert(t1 == it->time);
    mnc_sleep(1);
    mnc_update_item(it, true);
    assert(t1 != it->time);

    st_d_print("BEFORE:%lu AFTER:%lu", t1, it->time);

    mnc_unlink_item_l(it);
    mnc_remove_item(it);

    mnc_mem_cleanup();

    st_d_print("DONE!");

    return RET_YES;
}
