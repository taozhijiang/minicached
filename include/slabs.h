#ifndef __SLABS_H_
#define __SLABS_H_

#include "minicached.h"

#ifdef __cplusplus 
extern "C" {
#endif //__cplusplus 

/**
 * 这里总结一下，本系统用了三套锁，分别是:
 *  pthread_mutex_t *mnc_item_locks;
 *  pthread_mutex_t slab_lock;
 *  slabclass_t.lru_lock;
 *  
 *  为了防止死锁，必须按照上面的顺序按需求加锁
 */


//分配、释放slab操作时候的大锁
extern pthread_mutex_t slab_lock;

// sizes包含item的头部，所以不是实际的负载大小
enum mnc_slab_sz_index  {
    SZ_64 = 0, SZ_128, SZ_256, SZ_512, 
    SZ_1K, SZ_2K, SZ_4K, SZ_8K, SZ_16K, SZ_32K, SZ_64K, 
    SZ_128K, SZ_256K, SZ_512K, SZ_1M,
};
static const unsigned int SZ_1M_R = 1*1024*1024;

typedef struct {
    unsigned int size;      /* sizes of items */
    unsigned int perslab;   /* how many items per slab */

    void *slots;             /* list of item ptrs */
    unsigned int sl_curr;   /* total free items in list */

    unsigned int slabs;     /* how many slabs were allocated for this class */
    void **slab_list;       /* array of slab pointers */
    unsigned int slab_list_size; /* size of prev array */

    size_t requested;      /* The number of requested bytes */
                            // 当前class类别已经被缓存的对象占用的字节数目，
                            // 实际负载的字节数目

    // 保护每个class的LRU队列
    pthread_mutex_t lru_lock; 

} slabclass_t;


RET_T mnc_slab_init(void);
int mnc_slabs_clsid(const size_t size);
RET_T mnc_slabs_free(void *ptr, size_t size, unsigned int id);
void *mnc_slabs_alloc(size_t size, unsigned int id, unsigned int flags);

void mnc_class_statistic(unsigned int id);
unsigned int mnc_item_slab_size(const mnc_item* it);

#ifdef __cplusplus 
}
#endif //__cplusplus 

#endif //__SLABS_H_