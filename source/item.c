#include "minicached.h"
#include "hash_lru.h"

#include "slabs.h"

//对应处理hash容器中链表的保护
pthread_mutex_t *mnc_item_locks;

mnc_item* mnc_do_get_item(const void* key, const size_t nkey);
static void mnc_do_remove_item(mnc_item *it);
static void mnc_do_update_item(mnc_item *it, bool force);

RET_T mnc_items_init(void)
{
    unsigned int i = 0;
    unsigned int item_cnt = hashsize(HASH_POWER);

    mnc_item_locks = (pthread_mutex_t *)calloc(item_cnt, sizeof(pthread_mutex_t));
    if (!mnc_item_locks) 
    {
        st_d_error("Calloc for item failed! [%lu*%u]!", sizeof(pthread_mutex_t), item_cnt);
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
// mnc_item *mnc_new_item(const void *key, size_t nkey, time_t exptime, int nbytes);
// mnc_item* mnc_get_item_l(const void* key, const size_t nkey);
// void mnc_link_item_l(mnc_item *it);
// void mnc_unlink_item_l(mnc_item *it);
// RET_T mnc_store_item_l(mnc_item **it, const void* dat, const size_t ndata);
// void mnc_remove_item(mnc_item *it);
// void mnc_update_item(mnc_item *it, bool force);
// 

// 创建新的item
// 内部比较的复杂，设计到内存的分配、回收等操作
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
        st_d_error("Malloc slabs item error!");
        return NULL;
    }

    memset(ITEM_key(it), 0, nkey + nbytes + 1);
    it->nkey = nkey;
    it->ndata = 0;
    memcpy(ITEM_key(it), key, nkey);
    if (exptime)
    {
        it->exptime = exptime + mnc_status.current_time;   //绝对事件
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

    __sync_fetch_and_add(&mnc_status.request_cnt, 1);

    item_lock(hv);
    ret_i = mnc_do_get_item(key, nkey);
    if (ret_i)
        __sync_fetch_and_add(&mnc_status.hit_cnt, 1);
    item_unlock(hv);

    return ret_i;
}


void mnc_link_item_l(mnc_item *it)
{
    uint32_t hv = hash(ITEM_key(it), it->nkey);

    item_lock(hv);
    mnc_do_hash_insert(it);
    mnc_lru_insert(it);
    item_unlock(hv);

    return;
}

void mnc_unlink_item_l(mnc_item *it)
{
    uint32_t hv = hash(ITEM_key(it), it->nkey);

    item_lock(hv);
    it->it_flags &= ~ITEM_LINKED;

    mnc_do_hash_delete(it);
    mnc_lru_delete(it);
    item_unlock(hv);

    return;
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

    if (mnc_item_slab_size(p_it) >= ndata) 
    {
        memcpy(ITEM_dat(p_it), dat, ndata);
        p_it->ndata = ndata;
        if (!old_it)
        {
            mnc_do_hash_insert(p_it);
            mnc_lru_insert(p_it);
        }
        else
        {
            mnc_do_update_item(p_it, true);
            assert(p_it == old_it);
        }
        item_unlock(hv);
        return RET_YES;
    }

    st_d_print("重新更新内存块！");

    // 重新分配空间情况
    mnc_item *new_it = mnc_new_item(ITEM_key(p_it), p_it->nkey, p_it->exptime, ndata); 
    if (!new_it)
    {
        st_d_error("新分配错误%lu！", ndata);
        item_unlock(hv);
        return RET_NO;
    }

    if(old_it)
    {
        assert(p_it == old_it);
        mnc_do_hash_delete(p_it); 
        mnc_lru_delete(p_it);
    }
    mnc_do_remove_item(p_it);

    memcpy(ITEM_dat(new_it), dat, ndata);

    mnc_do_hash_insert(new_it);
    mnc_lru_insert(new_it);
    (*it) = new_it; // 保存新的地址信息

    item_unlock(hv);

    return RET_YES;
}


// 必须是没有被删除的，而且是非LINKED的
void mnc_remove_item(mnc_item *it)
{
    uint32_t hv = hash(ITEM_key(it), it->nkey);

    item_lock(hv);
    mnc_do_remove_item(it);
    item_unlock(hv);

    return;
}

/**
 * 更新LRU列表和元素的访问时间 
 * force是强制更新时间，否则防止短时间内反复更新时间和操作 
 * LRU列表，降低程序的性能 
 */
void mnc_update_item(mnc_item *it, bool force)
{
    uint32_t hv = hash(ITEM_key(it), it->nkey);

    item_lock(hv);
    mnc_do_update_item(it, force);
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

    ret_i = mnc_do_hash_find(key, nkey);

    if (ret_i && ret_i->exptime &&  ret_i->exptime <= mnc_status.current_time) 
    {
        mnc_do_hash_delete(ret_i);
        mnc_lru_delete(ret_i);
        mnc_do_remove_item(ret_i);
        ret_i = NULL;
    }

    return ret_i;
}

/**
 * 删除对象，并放回到slab缓存池中
 */
static void mnc_do_remove_item(mnc_item *it)
{
    if (it)
    {
        assert((it->it_flags & ITEM_LINKED) == 0);
        assert((it->it_flags & ITEM_SLABBED) == 0);

        st_d_print("FREEING(%x)...",  
                   hash(ITEM_key(it), it->nkey));
        
        mnc_slabs_free(it, 
                   ITEM_alloc_len(it->nkey, it->ndata), it->slabs_clsid);
    }

    return;
}

// 更新item->time的LRU访问时间
// work as touch
static void mnc_do_update_item(mnc_item *it, bool force)
{
    if ((it->time > mnc_status.current_time - ITEM_UPDATE_INTERVAL) && !force) 
        return;

    // 更新LRU队列顺序
    if (it->it_flags & ITEM_LINKED)
    {
        mnc_lru_delete(it);
        it->time = mnc_status.current_time;
        mnc_lru_insert(it);
    }

    return;
}

