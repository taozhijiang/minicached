#ifndef __MINICACHED_H_
#define __MINICACHED_H_

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <assert.h>

#include "st_others.h"

/**
 * 不仅仅是头部，负载也放置到这个结构体里面 
 * dat中只考虑二进制情况，所以数据如果本身带NULL，也需要计入， 
 * 中间的一个NULL是PAD用的
 */
typedef struct _mnc_item {
    time_t      time;       /* least recent access */
    time_t      exptime;    /* expire time */

    struct _mnc_item   *h_next; // hash链表

    uint8_t  nkey;   /* key len, not include null pad */
    uint8_t  ndata;  /* data len*/

    uint8_t  data[];    // key + NULL + data, [] not include in sizeof
} mnc_item, *p_mnc_item;

extern volatile time_t    current_time;

#define ITEM_key(item)  ((char*) &((item)->data)) 
#define ITEM_dat(item)  ((char*) &((item)->data) + (item)->nkey + 1 )
#define ITEM_alloc_len(nkey, ndata) (sizeof(mnc_item) + nkey + 1 + ndata)

extern RET_T mnc_init();
void mnc_item_test(void);


/**
 * TOTAL ITEM API
 */
mnc_item *mnc_new_item(const char *key, size_t nkey, time_t exptime, int nbytes);
mnc_item* mnc_get_item_l(const void* key, const size_t nkey);
RET_T mnc_link_item_l(mnc_item *it);
RET_T mnc_unlink_item_l(mnc_item *it);
RET_T mnc_store_item_l(mnc_item **it, const void* dat, const size_t ndata);
void mnc_remove_item(mnc_item *it);

#endif