#include "slabs.h"
#include "hash_lru.h"

size_t mem_limit = 0;     //最大内存限制
static size_t mem_allocated = 0;  //已经使用内存

slabclass_t mnc_slabclass[SLAB_SZ_TYPE]; 


RET_T mnc_slab_init(void)
{
    unsigned int i = 0;
    memset(mnc_slabclass, 0, sizeof(mnc_slabclass));

    unsigned int curr_size = 64;    //最小item size
    for (i=0; i<SLAB_SZ_TYPE; ++i)
    {
        mnc_slabclass[i].size = curr_size;
        mnc_slabclass[i].perslab = SZ_1M_R / mnc_slabclass[i].size;
        st_d_print("slab class %3d: chunk size %9u perslab %7u",
                i, mnc_slabclass[i].size, mnc_slabclass[i].perslab);

        pthread_mutex_init(&mnc_slabclass[i].sbclass_lock, NULL);
        curr_size = curr_size << 1;
    }

    mem_allocated = 0;

    return RET_YES;
}


int mnc_slabs_clsid(const size_t size) 
{
    int i = 0;

    for (i=0; i<SLAB_SZ_TYPE; ++i)
    {
        if(mnc_slabclass[i].size >= size)
            return i;
    }

    st_d_error("TOO LARGE: %lu", size);
    return -1;
}

unsigned int mnc_item_slab_size(const mnc_item* it) 
{
    assert(it->slabs_clsid < SLAB_SZ_TYPE);

    return mnc_slabclass[it->slabs_clsid].size;
}

static void *mnc_do_slabs_alloc(size_t size, unsigned int id, unsigned int flags);
static RET_T mnc_do_slabs_free(void *ptr, size_t size, unsigned int id);
static RET_T mnc_do_slabs_newslab(unsigned int id);

void *mnc_slabs_alloc(size_t size, unsigned int id, unsigned int flags) 
{
    void *ret;

    pthread_mutex_lock(&mnc_slabclass[id].sbclass_lock); 
    ret = mnc_do_slabs_alloc(size, id, flags);
    pthread_mutex_unlock(&mnc_slabclass[id].sbclass_lock);

    return ret;
}

RET_T mnc_slabs_free(void *ptr, size_t size, unsigned int id) 
{
    RET_T ret;

    pthread_mutex_lock(&mnc_slabclass[id].sbclass_lock);
    ret = mnc_do_slabs_free(ptr, size, id);
    pthread_mutex_unlock(&mnc_slabclass[id].sbclass_lock);

    return ret;
}


// Internal API without lock

static void *mnc_do_slabs_alloc(size_t size, unsigned int id, unsigned int flags) 
{
    assert(id < SLAB_SZ_TYPE);
    slabclass_t *p_class = &mnc_slabclass[id];
    mnc_item    *it;

    /*无空闲item*/
    if (p_class->sl_curr == 0) 
    {
        if (mnc_do_slabs_newslab(id) == RET_NO)
            return NULL;
    }

    assert(p_class->sl_curr);

    it = (mnc_item *)p_class->slots;
    p_class->slots = it->next;
    if (it->next) it->next->prev = 0;
    /* Kill flag and initialize refcount here for lock safety in slab
     * mover's freeness detection. */
    it->it_flags &= ~ITEM_SLABBED;
    it->slabs_clsid = id;
    p_class->sl_curr --;

    p_class->requested += size;

    return it;
}

// 并非释放内存，而是进行初始化操作，变为缓存可用的item对象
static RET_T mnc_do_slabs_free(void *ptr, size_t size, unsigned int id)
{
    slabclass_t *p_class;
    mnc_item    *it;

    p_class = &mnc_slabclass[id];

    it = (mnc_item *)ptr;
    it->it_flags = ITEM_SLABBED;
    it->slabs_clsid = 0;    //将会在申请的时候重新初始化

    // 添加到链表的头部
    it->prev = 0;
    it->next = p_class->slots;
    if (it->next) it->next->prev = it;
    p_class->slots = it;

    p_class->sl_curr++;
    p_class->requested -= size;

    return RET_YES;
}


static RET_T mnc_do_slabs_newslab(unsigned int id)
{
    slabclass_t *p_class = &mnc_slabclass[id];
    unsigned int i = 0;
    unsigned int slab_len = p_class->perslab * p_class->size;
    
    if (( slab_len + mem_allocated) > mem_limit) 
    {
        st_d_error("out of memory: already %lu, request %d, total %lu", mem_allocated,
                   p_class->perslab * p_class->size, mem_limit);
        return RET_NO; 
    }

    void* new_block = malloc(slab_len);
    if (!new_block)
    {
        st_d_error("Malloc %d failed!", slab_len);
        return RET_NO;
    }

    if (p_class->slabs == p_class->slab_list_size) 
    {
        int new_list_size = 0;
        if (p_class->slab_list_size == 0) 
            new_list_size = mem_limit / slab_len / 3;
        else
            new_list_size = p_class->slab_list_size * 2;

       // The contents will  be  unchanged
       // in  the range from the start of the 
       // region up to the minimum of the old and new sizes.
        void *new_list = realloc(p_class->slab_list, new_list_size * sizeof(void *));
        if (!new_list)
        {
            st_d_error("relarge the slab_list failed: %d, class_id: %d", 
                       new_list_size, id);
            free(new_block);
            return RET_NO;
        }
        else
        {
            st_d_print("relarge the slab_list ok: %d, class_id:%d", 
                       new_list_size, id);
            p_class->slab_list = new_list;
            p_class->slab_list_size = new_list_size;
        }
    }

    mem_allocated += slab_len;
    memset(new_block, 0, (size_t)slab_len);
    p_class->slab_list[p_class->slabs++] = new_block;

    for (i = 0; i < p_class->perslab; i++) 
    {
        mnc_do_slabs_free(new_block, 0/*未使用*/, id);
        new_block += p_class->size;
    }

    return RET_YES;
}


void mnc_class_statistic(unsigned int id)
{
    slabclass_t* p_class = &mnc_slabclass[id];

    st_d_print("=========================================");
    st_d_print("class_id: %lu", id);
    st_d_print("item size: %x", p_class->size);
    st_d_print("perslab count: %lu", p_class->perslab); 
    st_d_print("free count: %lu", p_class->sl_curr); 
    st_d_print("alloc slab count: %lu", p_class->slabs); 
    st_d_print("slab list ptr count: %lu", p_class->slab_list_size); 
    st_d_print("requested bytes: %lu", p_class->requested);

    st_d_print("total memory: %lu, already used memory:%lu", mem_limit, mem_allocated); 
    st_d_print("=========================================");

    return;
}

