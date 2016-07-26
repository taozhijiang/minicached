#ifndef __HASH_LRU_H_
#define __HASH_LRU_H_

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#include "minicached.h"

#ifdef __cplusplus 
extern "C" {
#endif //__cplusplus 

#include "slabs.h"

#ifndef SLAB_SZ_TYPE
#define SLAB_SZ_TYPE (SZ_1M + 1)
#endif


uint32_t jenkins_hash(const void *key, size_t length);
uint32_t MurmurHash3_x86_32(const void *key, size_t length);

typedef uint32_t (*hash_func)(const void *key, size_t length);
extern hash_func hash;

#define HASH_POWER 10
#define hashsize(n) ((unsigned long int)1<<(n))
#define hashmask(n) (hashsize(n)-1)


extern pthread_mutex_t *mnc_item_locks;

// return 0 on success, trylock should check 
static inline int item_lock(uint32_t hv) {
    return pthread_mutex_lock(&mnc_item_locks[hv & hashmask(HASH_POWER)]);
}

static inline int item_unlock(uint32_t hv) {
    return pthread_mutex_unlock(&mnc_item_locks[hv & hashmask(HASH_POWER)]);
}

static inline int item_trylock(uint32_t hv) {
    return pthread_mutex_trylock(&mnc_item_locks[hv & hashmask(HASH_POWER)]);
}

static inline int item_tryunlock(uint32_t hv) {
    return pthread_mutex_unlock(&mnc_item_locks[hv & hashmask(HASH_POWER)]);
}

extern RET_T mnc_hash_init(void);

mnc_item* mnc_do_hash_find(const void* key, const size_t nkey);
RET_T mnc_do_hash_insert(mnc_item *it);
RET_T mnc_do_hash_delete(mnc_item *it);


/*
 * We only reposition items in the LRU queue if they haven't been repositioned
 * in this many seconds. That saves us from churning on frequently-accessed
 * items. 
 * 防止过于频繁的LRU更新
 */
#define ITEM_UPDATE_INTERVAL 60

// LRU related item
RET_T mnc_lru_init(void);
void mnc_lru_insert(mnc_item *it);
void mnc_lru_delete(mnc_item *it);

// 内存回收函数
extern void mnc_do_expired(unsigned int id);
extern void mnc_do_lru_trim(unsigned int id);

#ifdef __cplusplus 
}
#endif //__cplusplus 


#endif //__HASH_LRU_H_