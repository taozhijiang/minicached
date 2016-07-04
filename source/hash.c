#include "minicached.h"
#include "hash.h"

/* Main hash table. This is where we look except during expansion. */
static mnc_item** primary_hashtable = 0;
hash_func hash;

RET_T mnc_hash_init(void)
{
    // using jenkins_hash
    hash = jenkins_hash;

    primary_hashtable = calloc(hashsize(HASH_POWER), sizeof(void *));
    if (! primary_hashtable) 
    {
        st_d_error("Failed to init hashtable.\n");
        return RET_NO;
    }

    return RET_YES;
}

mnc_item* hash_find(const void* key, const size_t nkey)
{
    mnc_item *head = NULL;
    mnc_item *it = NULL;
    uint32_t hv = hash(key, nkey);

    head = primary_hashtable[hv & hashmask(HASH_POWER)]; 

    while (head) 
    {
        if ((nkey == head->nkey) && (memcmp(key, ITEM_key(head), nkey) == 0)) {
            it = head;
            break;
        }
        head = head->h_next;
    }

    return it;
}

/*查找到对应元素列表的前一个位置，主要是删除时候使用*/
static mnc_item** hash_find_pre(const void* key, const size_t nkey)
{
    mnc_item **p_pos = NULL;
    uint32_t hv = hash(key, nkey);

    p_pos = &primary_hashtable[hv & hashmask(HASH_POWER)]; 

    while (*p_pos && ((nkey != (*p_pos)->nkey) || memcmp(key, ITEM_key(*p_pos), nkey))) {
        p_pos = &(*p_pos)->h_next;
    }
    return p_pos;
}


/* item_lock already hold before*/

RET_T mnc_hash_lru_insert(mnc_item *it)
{
    mnc_item* head = NULL;
    uint32_t hv = hash(ITEM_key(it), it->nkey); 

    primary_hashtable[hv & hashmask(HASH_POWER)]; 
    it->h_next = primary_hashtable[hv & hashmask(HASH_POWER)] = it;

    return RET_YES;
}

RET_T mnc_hash_lru_delete(mnc_item *it)
{
    mnc_item **before = hash_find_pre(ITEM_key(it), it->nkey); 
    mnc_item *nxt = NULL;

    if (before)
    {
        nxt = (*before)->h_next;
        (*before)->h_next = 0;   /* probably pointless, but whatever. */
        *before = nxt;

        return RET_YES;
    }

    return RET_NO;
}

// destroy everything linked in the hashtable
RET_T mnc_hash_destroy(void)
{
    return RET_YES;
}
