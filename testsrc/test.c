#include "minicached.h"
#include "hash.h"

RET_T mnc_item_test_char_key(void);
RET_T mnc_item_test_int_key(void);

int main(int argc, char* argv[])
{
    mnc_init();
    mnc_item_test_int_key();
    mnc_item_test_char_key();

    while (1)
    {
        sleep(3);
    }
    return 0;
}

RET_T mnc_item_test_char_key(void)
{
    char* key ="桃子";
    unsigned int key_len = strlen(key) + 1;
    char* msg = "桃子的值还是桃子！";
    char* msg2 = "桃子的值还是桃子他大爷的啊！";
    mnc_item* it = NULL;
    mnc_item* g_it = NULL;
    mnc_item* g_it_old = NULL;


    it = mnc_new_item(key, key_len, 5, strlen(msg)+1);
    mnc_link_item_l(it);
    mnc_store_item_l(&it, msg, strlen(msg)+1);
    g_it = mnc_get_item_l(key, key_len);
    assert(g_it);
    st_d_print("VALUE1:%s", ITEM_dat(g_it)); 

    g_it_old = g_it; // should be reallocated
    mnc_store_item_l(&g_it, msg, strlen(msg2)+1);
    g_it = mnc_get_item_l(key, key_len);
    assert(g_it);
    assert(g_it != g_it_old);
    st_d_print("VALUE2:%s", ITEM_dat(g_it)); 

    st_d_print("%s PASS!", __FUNCTION__);

    return RET_YES;
}


RET_T mnc_item_test_int_key(void)
{
    int i_key = 12333;
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

    g_it_old = g_it;    // should be reallocated
    mnc_store_item_l(&g_it, msg, strlen(msg2)+1);
    g_it = mnc_get_item_l(&i_key, key_len);
    assert(g_it);
    assert(g_it != g_it_old);
    st_d_print("VALUE2:%s", ITEM_dat(g_it)); 

    sleep(7);
    assert(!mnc_get_item_l(&i_key, key_len));

    st_d_print("expired item can not be got!");

    st_d_print("%s PASS!", __FUNCTION__);

    return RET_YES;
}

