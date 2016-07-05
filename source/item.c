#include "minicached.h"
#include "hash_lru.h"

#include "slabs.h"

static pthread_mutex_t *mnc_item_locks;

static inline void item_lock(uint32_t hv) {
    pthread_mutex_lock(&mnc_item_locks[hv & hashmask(HASH_POWER)]);
}

static inline void item_unlock(uint32_t hv) {
    pthread_mutex_unlock(&mnc_item_locks[hv & hashmask(HASH_POWER)]);
}


mnc_item* mnc_do_get_item(const void* key, const size_t nkey);
static void mnc_do_remove_item(mnc_item *it);

RET_T mnc_items_init(void)
{
    unsigned int i = 0;
    unsigned int item_cnt = hashsize(HASH_POWER);

    mnc_item_locks = (pthread_mutex_t *)calloc(item_cnt, sizeof(pthread_mutex_t));
    if (!mnc_item_locks) 
    {
        st_d_error("Calloc for item failed! [%d*%d]!", sizeof(pthread_mutex_t), item_cnt);
        return RET_NO;
    }

    for (i=0; i<item_cnt; ++i)
    {
        pthread_mutex_init(&mnc_item_locks[i], NULL);
    }

    return RET_YES;
}

/**
 * MNC_ITEM API 
 * 带_l后缀的，是可能修改hashtable/LRU列表的，使用时候需要注意 
 */
// 创建新的item
mnc_item *mnc_new_item(const void *key, size_t nkey, time_t exptime, int nbytes) 
{
    mnc_item *it;
    /* do_item_alloc handles its own locks */

    int class_id = mnc_slabs_clsid(ITEM_alloc_len(nkey, nbytes));
    if (class_id < 0)
        return NULL;

    it = (mnc_item *)mnc_slabs_alloc(ITEM_alloc_len(nkey, nbytes), class_id, 0);
    if (!it)
    {
        st_d_error("Malloc for item error!");
        return NULL;
    }

    memset(it, 0, ITEM_alloc_len(nkey, nbytes));
    it->nkey = nkey;
    it->ndata = nbytes;
    memcpy(ITEM_key(it), key, nkey);
    if (exptime)
    {
        it->exptime = exptime + current_time;   //绝对事件
    }
    else
    {
        it->exptime = 0; //never expire
    }

    return it;
}

// if it hasn't been marked as expired,
// be sure item already in the hashtable and LRU list

mnc_item* mnc_get_item_l(const void* key, const size_t nkey)
{   
    mnc_item* ret_i = NULL;
    uint32_t hv = hash(key, nkey);

    item_lock(hv);
    ret_i = mnc_do_get_item(key, nkey);
    item_unlock(hv); 

    return ret_i;
}


RET_T mnc_link_item_l(mnc_item *it)
{
    RET_T ret;
    uint32_t hv = hash(ITEM_key(it), it->nkey);

    item_lock(hv);
    ret = mnc_hash_insert(it);
    mnc_lru_insert(it);
    item_unlock(hv);

    return ret;
}

RET_T mnc_unlink_item_l(mnc_item *it)
{
    RET_T ret;
    uint32_t hv = hash(ITEM_key(it), it->nkey);

    item_lock(hv);
    ret = mnc_hash_delete(it);
    mnc_lru_delete(it);
    item_unlock(hv);

    return ret;
}

// do store stuffs
// if item already in hashtable, update it!
// else insert it to hashtable
// 注意！！！ it的地址可能被更新
RET_T mnc_store_item_l(mnc_item **it, const void* dat, const size_t ndata)
{
    uint32_t hv;
    mnc_item *old_it = NULL;
    mnc_item *p_it = *it;
    RET_T ret = RET_NO;

    if (!it || !p_it)
    {
        st_d_error("WHETHER STORE OR UPADTE, PLEASE ALLOCATE A NEW ITEM FIRST!");
        return RET_NO;
    }

    hv = hash(ITEM_key(p_it), p_it->nkey);
    item_lock(hv);
    old_it = mnc_do_get_item(ITEM_key(p_it), p_it->nkey);   //检查是否已经加入链表

    if (p_it->ndata >= ndata) 
    {
        memcpy(ITEM_dat(p_it), dat, ndata);
        p_it->ndata = ndata;
        if (!old_it)
        {
            mnc_hash_insert(p_it);
            mnc_lru_insert(p_it);
        }
        assert(p_it == old_it);
        item_unlock(hv);
        return RET_YES;
    }

    st_d_print("重新更新内存块！");

    // 重新分配空间情况
    mnc_item *new_it = mnc_new_item(ITEM_key(p_it), p_it->nkey, p_it->exptime, ndata); 
    if (!new_it)
    {
        st_d_error("新分配错误%d！", ndata);
        item_unlock(hv);
        return RET_NO;
    }

    if(!old_it)
    {
        assert(p_it == old_it);
        mnc_hash_delete(p_it); 
        mnc_lru_delete(p_it);
    }
    mnc_do_remove_item(p_it);

    memcpy(ITEM_dat(new_it), dat, ndata);

    mnc_hash_insert(new_it);
    mnc_lru_insert(new_it);
    (*it) = new_it;

    item_unlock(hv);

    return RET_YES;
}


void mnc_remove_item(mnc_item *it)
{
    uint32_t hv = hash(ITEM_key(it), it->nkey);

    item_lock(hv);
    mnc_do_remove_item(it);
    item_unlock(hv);

    return;
}



/**
 * MNC_ITEM internel interface and api
 * 不带锁的操作结果，用的非递归互斥锁
 */

mnc_item* mnc_do_get_item(const void* key, const size_t nkey)
{   
    mnc_item* ret_i = NULL;
    uint32_t hv = hash(key, nkey);

    ret_i = hash_find(key, nkey);

    if (ret_i && ret_i->exptime &&  ret_i->exptime <= current_time) 
    {
        st_d_print("[%lx]expired", hv);
        mnc_hash_delete(ret_i);
        mnc_lru_delete(ret_i);
        mnc_do_remove_item(ret_i);
        ret_i = NULL;
    }

    return ret_i;
}

static void mnc_do_remove_item(mnc_item *it)
{
    if (it)
    {
        st_d_print("FREEING(%lx)...",  
                   hash(ITEM_key(it), it->nkey));
        
        mnc_slabs_free(it, 
                   ITEM_alloc_len(it->nkey, it->ndata), it->slabs_clsid);
    }

    return;
}

