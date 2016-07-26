#include "minicached.h"
#include "hash_lru.h"

// 按照从头到尾的LRU访问链表
// 每个slabclass对应一个LRU访问链表，用slabclass.lru_lock进行保护
static mnc_item *lru_heads[SLAB_SZ_TYPE];
static mnc_item *lru_tails[SLAB_SZ_TYPE];
extern slabclass_t mnc_slabclass[SLAB_SZ_TYPE];

static RET_T mnc_do_lru_insert(mnc_item *it);
static RET_T mnc_do_lru_delete(mnc_item *it);

RET_T mnc_lru_init(void)
{
    memset(lru_heads, 0, sizeof(lru_heads));
    memset(lru_tails, 0, sizeof(lru_tails));

    return RET_YES;
}

RET_T mnc_lru_insert(mnc_item *it)
{
    RET_T ret;
    unsigned int id = it->slabs_clsid;

    assert(id < SLAB_SZ_TYPE);

    pthread_mutex_lock(&mnc_slabclass[id].lru_lock);
    ret = mnc_do_lru_insert(it);
    pthread_mutex_unlock(&mnc_slabclass[id].lru_lock);

    return ret;
}


RET_T mnc_lru_delete(mnc_item *it)
{
    RET_T ret;
    unsigned int id = it->slabs_clsid;

    assert(id < SLAB_SZ_TYPE);

    pthread_mutex_lock(&mnc_slabclass[id].lru_lock);
    ret = mnc_do_lru_delete(it);
    pthread_mutex_unlock(&mnc_slabclass[id].lru_lock);

    return ret;
}


// Internel API

static RET_T mnc_do_lru_insert(mnc_item *it)
{
    mnc_item **head, **tail;
    assert((it->it_flags & ITEM_SLABBED) == 0);

    head = &lru_heads[it->slabs_clsid];
    tail = &lru_tails[it->slabs_clsid];

    assert(it != *head);
    assert((*head && *tail) || (*head == 0 && *tail == 0));

    it->prev = 0;
    it->next = *head;
    if (it->next) 
        it->next->prev = it;

    *head = it;
    if (*tail == 0) 
        *tail = it;

    return RET_YES;
}

static RET_T mnc_do_lru_delete(mnc_item *it)
{
    mnc_item **head, **tail;
    head = &lru_heads[it->slabs_clsid];
    tail = &lru_tails[it->slabs_clsid];

    if (*head == it) {
        assert(it->prev == 0);
        *head = it->next;
    }
    if (*tail == it) {
        assert(it->next == 0);
        *tail = it->prev;
    }

    //双向非循环链表
    assert(it->next != it);
    assert(it->prev != it);

    if (it->next) 
        it->next->prev = it->prev;
    if (it->prev) 
        it->prev->next = it->next;

    return RET_YES;
}

extern mnc_item* mnc_do_fetch_expired(unsigned int id)
{
    mnc_item *ptr = lru_heads[id];

    for (; ptr != NULL; ptr = ptr->next) 
    {
        if (ptr->exptime &&  ptr->exptime <= mnc_status.current_time) 
        {
            mnc_do_lru_delete(ptr);
            return ptr;
        }
    }

    return NULL;
}

extern mnc_item* mnc_do_fetch_lru_last(unsigned int id)
{
    mnc_item* ret = NULL;

    if (lru_tails[id])
    {
        ret = lru_tails[id];
        mnc_do_lru_delete(ret);
    }

    return ret;
}
