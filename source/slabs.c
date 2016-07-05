#include "slabs.h"

static size_t mem_limit = 0;     //最大内存限制
static size_t mem_allocated = 0;  //已经使用内存

#define SLAB_SZ_NUM (SZ_1M + 1)
static slabclass_t mnc_slabclass[SLAB_SZ_NUM]; 


RET_T mnc_slab_init(void)
{
    unsigned int i = 0;
    memset(mnc_slabclass, 0, sizeof(mnc_slabclass));

    mem_limit = 64 * SZ_1M_R;

    unsigned int curr_size = 64;
    for (i=0; i<SLAB_SZ_NUM; ++i)
    {
        mnc_slabclass[i].size = curr_size;
        mnc_slabclass[i].perslab = SZ_1M_R / mnc_slabclass[i].size;
        st_d_print("slab class %3d: chunk size %9u perslab %7u",
                i, mnc_slabclass[i].size, mnc_slabclass[i].perslab);

        pthread_mutex_init(&mnc_slabclass[i].sbclass_lock, NULL);
        curr_size = curr_size << 1;
    }

    return RET_YES;
}


int mnc_slabs_clsid(const size_t size) 
{
    int i = 0;

    for (i=0; i<SLAB_SZ_NUM; ++i)
    {
        if(mnc_slabclass[i].size >= size)
            return i;
    }

    st_d_error("TOO LARGE: %d", size);
    return -1;
}

static void *do_mnc_slabs_alloc(size_t size, unsigned int id, unsigned int flags);
static RET_T do_mnc_slabs_free(void *ptr, size_t size, unsigned int id);
static RET_T do_mnc_slabs_newslab(unsigned int id);
void *mnc_slabs_alloc(size_t size, unsigned int id, unsigned int flags) 
{
    void *ret;

    pthread_mutex_lock(&mnc_slabclass[id].sbclass_lock); 
    ret = do_mnc_slabs_alloc(size, id, flags);
    pthread_mutex_unlock(&mnc_slabclass[id].sbclass_lock);

    return ret;
}

RET_T mnc_slabs_free(void *ptr, size_t size, unsigned int id) 
{
    RET_T ret;

    pthread_mutex_lock(&mnc_slabclass[id].sbclass_lock);
    ret = do_mnc_slabs_free(ptr, size, id);
    pthread_mutex_unlock(&mnc_slabclass[id].sbclass_lock);

    return ret;
}


// Internal API without lock

static void *do_mnc_slabs_alloc(size_t size, unsigned int id, unsigned int flags) 
{
    assert(id < SLAB_SZ_NUM);
    slabclass_t *p_class = &mnc_slabclass[id];
    mnc_item    *it;

    /*无空闲item*/
    if (p_class->sl_curr == 0) 
    {
        if (do_mnc_slabs_newslab(id) == RET_NO)
            return NULL;
    }

    assert(p_class->sl_curr);

    it = (mnc_item *)p_class->slots;
    p_class->slots = it->next;
    if (it->next) it->next->prev = 0;
    /* Kill flag and initialize refcount here for lock safety in slab
     * mover's freeness detection. */
    it->it_flags &= ~ITEM_SLABBED;
    p_class->sl_curr --;

    p_class->requested -= size;

    return it;
}

// 并非释放内存，而是进行初始化操作，变为缓存可用的item对象
static RET_T do_mnc_slabs_free(void *ptr, size_t size, unsigned int id)
{
    slabclass_t *p_class;
    mnc_item    *it;

    p_class = &mnc_slabclass[id];

    it = (mnc_item *)ptr;
    it->it_flags = ITEM_SLABBED;
    it->slabs_clsid = 0;    //将会在item中初始化

    // 添加到链表的头部
    it->prev = 0;
    it->next = p_class->slots;
    if (it->next) it->next->prev = it;
    p_class->slots = it;

    p_class->sl_curr++;
    p_class->requested -= size;

    return RET_YES;
}


static RET_T do_mnc_slabs_newslab(unsigned int id)
{
    slabclass_t *p_class = &mnc_slabclass[id];
    unsigned int i = 0;
    unsigned int slab_len = p_class->perslab * p_class->size;
    
    if (( slab_len + mem_allocated) > mem_limit) 
    {
        st_d_error("out of memory: already %d, request %d, total %d", mem_allocated,
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

        st_d_print("relarge the slab_list to: %d", new_list_size);

       // The contents will  be  unchanged
       // in  the range from the start of the 
       // region up to the minimum of the old and new sizes.
        void *new_list = realloc(p_class->slab_list, new_list_size * sizeof(void *));
        if (!new_list)
        {
            st_d_error("relarge the slab_list failed: %d", new_list_size);
            free(new_block);
            return RET_NO;
        }
        else
        {
            st_d_print("relarge the slab_list ok: %d", new_list_size);
            p_class->slab_list = new_list;
            p_class->slab_list_size = new_list_size;
        }
    }

    mem_allocated += slab_len;
    memset(new_block, 0, (size_t)slab_len);
    p_class->slab_list[p_class->slabs++] = new_block;

    for (i = 0; i < p_class->perslab; i++) 
    {
        do_mnc_slabs_free(new_block, 0/*未使用*/, id);
        new_block += p_class->size;
    }

    return RET_YES;
}

