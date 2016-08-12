#include "slabs.h"
#include "hash_lru.h"

size_t minicached_mem_limit = 0;    //最大内存限制
static size_t mem_allocated = 0;    //已经使用内存

//分配、释放slab操作时候的大锁
pthread_mutex_t slab_lock;
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

        pthread_mutex_init(&mnc_slabclass[i].lru_lock, NULL);
        curr_size = curr_size << 1;
    }

    mem_allocated = 0;

    pthread_mutex_init(&slab_lock, NULL);

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
static RET_T mnc_do_slabs_move(mnc_item* it_src, mnc_item* it_des,
                               unsigned int id);
extern RET_T mnc_do_slabs_free(void *ptr, size_t size, unsigned int id);
static RET_T mnc_do_slabs_destroy(mnc_item* it, unsigned int id);
static RET_T mnc_do_slabs_newslab(unsigned int id);
static RET_T mnc_slabs_rebalance(unsigned int id);


extern void mnc_lru_expired(unsigned int id);
extern void mnc_lru_trim(unsigned int id);

/**
 * 将slabs的回收等任务从slabs_alloc中剥离出来，这样的好处是可以
 * 降低对slab_lock这个大锁的hold持有时间
 *
 * 该函数调用时候可能持有 hash锁
 */
void *mnc_slabs_alloc(size_t size, unsigned int id, unsigned int flags, int hold_lock)
{
    void *ret;
    RET_T r_t = RET_NO;

    pthread_mutex_lock(&slab_lock);
    ret = mnc_do_slabs_alloc(size, id, flags);
    pthread_mutex_unlock(&slab_lock);
    if (ret)
        return ret;

    st_d_print("collect expired items");
    mnc_lru_expired(id);
    pthread_mutex_lock(&slab_lock);
    ret = mnc_do_slabs_alloc(size, id, flags);
    pthread_mutex_unlock(&slab_lock);
    if (ret)
        return ret;

    pthread_mutex_lock(&slab_lock);
    st_d_print("alloc newslab");
    mnc_do_slabs_newslab(id);   //需要slab_lock保护
    ret = mnc_do_slabs_alloc(size, id, flags);
    pthread_mutex_unlock(&slab_lock);
    if (ret)
        return ret;

    // rebalance 比较的复杂，需要涉及到类似shuffle的操作，需要多次持有hash的锁来
    // 移动元素
    if (!hold_lock)
    {
        st_d_print("rebalance slab_class");
        r_t = mnc_slabs_rebalance(id);
        if(r_t == RET_YES)
        {
            pthread_mutex_lock(&slab_lock);
            mnc_do_slabs_newslab(id);   //需要slab_lock保护
            ret = mnc_do_slabs_alloc(size, id, flags);
            pthread_mutex_unlock(&slab_lock);

            if (ret)
                return ret;
        }
    }

    st_d_print("lru trim schema");
    mnc_lru_trim(id);
    pthread_mutex_lock(&slab_lock);
    ret = mnc_do_slabs_alloc(size, id, flags);
    pthread_mutex_unlock(&slab_lock);
    return ret;
}

RET_T mnc_slabs_free(void *ptr, size_t size, unsigned int id)
{
    RET_T ret;

    pthread_mutex_lock(&slab_lock);
    ret = mnc_do_slabs_free(ptr, size, id);
    pthread_mutex_unlock(&slab_lock);

    return ret;
}


// Internal API without lock
// 由于涉及到对象的回收，小心死锁!!!
// 这个函数可能被store_item等函数重新分配使用，是hold hash锁的，此处
// 注意死锁
// 线程1:  A  B  ->  B A
// 线程2:  A tryB -> tryB A 感觉不会死锁的吧
//
// slab_lock    held
// hv           may held
static void *mnc_do_slabs_alloc(size_t size, unsigned int id, unsigned int flags)
{
    assert(id < SLAB_SZ_TYPE);
    slabclass_t *p_class = &mnc_slabclass[id];
    mnc_item    *it;
    unsigned int i = 0;

    if(p_class->sl_curr == 0)
        return NULL;

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
extern RET_T mnc_do_slabs_free(void *ptr, size_t size, unsigned int id)
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

// 主要是释放内存的时候，或者强行item_move的时候，将其从
// slab_class空闲队列中删除
static RET_T mnc_do_slabs_destroy(mnc_item* it, unsigned int id)
{
    slabclass_t *p_class;
    p_class = &mnc_slabclass[id];

    // 确保释放的是没有保存数据的
    assert( (it->it_flags & ITEM_LINKED) == 0);
    assert( (it->it_flags & ITEM_PENDING) == 0);
    assert( (it->it_flags & ITEM_SLABBED) );

    it->slabs_clsid = 0;    //将会在申请的时候重新初始化

    // 从空闲链表中删除
    if (p_class->slots == it)
    {
        p_class->slots = it->next;
        if (it->next)
            it->next->prev = 0; // already the head one
    }
    else
    {
        if (it->next)
            it->next->prev = it->prev;
        if (it->prev)
            it->prev->next = it->next;
    }

    it->next = NULL;
    it->prev = NULL;
    p_class->sl_curr--;

    return RET_YES;
}

/**
 * 调用的函数负责hv加锁
 * it_src手动free/destroy
 */
static RET_T mnc_do_slabs_move(mnc_item* it_src, mnc_item* it_des,
                               unsigned int id)
{
    slabclass_t *p_class;
    p_class = &mnc_slabclass[id];

    assert(it_src && it_des);
    assert(it_src->it_flags & ITEM_LINKED);
    assert(it_des->it_flags & ITEM_SLABBED);

    mnc_do_hash_delete(it_src);
    mnc_lru_delete(it_src);

    // 从slots空闲链表中提取出来
    mnc_do_slabs_destroy(it_des, id);

    it_des->time = it_src->time;
    it_des->exptime = it_src->exptime;
    it_des->slabs_clsid = it_src->slabs_clsid;
    it_des->it_flags &= ~ITEM_SLABBED;
    it_des->it_flags |= ITEM_PENDING;
    it_des->nkey = it_src->nkey;
    it_des->ndata = it_src->ndata;

    memcpy(it_des->data, it_src->data, p_class->size-sizeof(mnc_item) );
    mnc_do_hash_insert(it_des);
    mnc_lru_insert(it_des);

    //st_d_print("MOVING FROM (%p) to (%p)...", it_src, it_des);

    return RET_YES;
}

static RET_T mnc_do_slabs_newslab(unsigned int id)
{
    slabclass_t *p_class = &mnc_slabclass[id];
    unsigned int i = 0;
    unsigned int slab_len = p_class->perslab * p_class->size;

    if (( slab_len + mem_allocated) > minicached_mem_limit)
    {
        st_d_error("out of memory: already %lu, request %d, total %lu", mem_allocated,
                   p_class->perslab * p_class->size, minicached_mem_limit);
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
            new_list_size = minicached_mem_limit / slab_len / 3;
        else
            new_list_size = p_class->slab_list_size * 2;

        if (new_list_size < 4)
            new_list_size = 8;

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

/**
 * 内存回收类函数
 *
 * 当slab_free之后，实际的item还是处于ITEM_SLABBED的状态，占用着内存。
 * 当ITEM_SLABBED的数目足够多，而有需要内存的时候，可以释放部分SLABBED对象
 * 整理出整块的大块内存，供给其它slab_class类型分配使用
 *
 * 已经保证调用的时候，没有持有任何的锁
 */

static RET_T mnc_do_slabs_recycle(unsigned int id, double stress)
{
    slabclass_t *p_class = &mnc_slabclass[id];
    unsigned int i = 0;
    unsigned int j = 0, k = 0;
    uint32_t hv = 0;
    mnc_item* it_free = NULL;

    assert(stress >= 1.0);

    if (p_class->sl_curr < p_class->perslab * stress)
        return RET_NO;

    // 只剩余一块的情况
    if (p_class->slabs == 1 && p_class->sl_curr == p_class->perslab)
    {
        st_d_print("FREE ONE EMPTY PAGE!");

        pthread_mutex_lock(&slab_lock);
        for (i=0; i<p_class->perslab; ++i) // free last block
        {
            it_free = (mnc_item *)((char *)p_class->slab_list[0] +
                                   i * p_class->size);

            // 已经是unlinked的状态了
            mnc_do_slabs_destroy(it_free, i);
        }

        st_d_print("GOOD, Free Block Page: %p Size:%d ", p_class->slab_list[0],
           (p_class->size * p_class->perslab) );

        free(p_class->slab_list[0]);
        p_class->slab_list[0] = NULL;
        -- p_class->slabs;
        mem_allocated -= p_class->size * p_class->perslab;

        pthread_mutex_unlock(&slab_lock);

        return RET_YES;
    }


    // DO IT!

    // 需要释放的块指针
    void *ptr_free = p_class->slab_list[p_class->slabs-1];

    // 空余slot查找用
    void *ptr_des = NULL;
    mnc_item* it_tmp = NULL;
    mnc_item* it_des = NULL;

    for (i=0; i<p_class->perslab; ++i) // free last block
    {
        it_free = (mnc_item *) ((char *)ptr_free + i*p_class->size);
        if( it_free->it_flags & ITEM_SLABBED )
        {
            pthread_mutex_lock(&slab_lock);
            mnc_do_slabs_destroy(it_free, id);
            pthread_mutex_unlock(&slab_lock);
        }
        else if (it_free->it_flags & ITEM_PENDING)
        {
            st_d_print("WARN!!!! >>> PENDING FREE....");

            mnc_remove_item(it_free);
            pthread_mutex_lock(&slab_lock);
            mnc_do_slabs_destroy(it_free, id);
            pthread_mutex_unlock(&slab_lock);
        }
        else if (it_free->it_flags & ITEM_LINKED)
        {
            hv = hash(ITEM_key(it_free), it_free->nkey);

            //寻找空余的slot，需要优化
            it_des = NULL;
            for (j=0; j<(p_class->slabs-1); ++j) //预留最后的一个空
            {
                ptr_des = p_class->slab_list[j];
                for (k=0; k<p_class->perslab; ++k)
                {
                    it_tmp = (mnc_item *) ((char *)ptr_des + k*p_class->size);
                    if (it_tmp->it_flags & ITEM_SLABBED)
                    {
                        it_des = it_tmp;
                        break;
                    }
                }

                if (it_des)
                    break;
            }

            assert(it_des);

            item_lock(hv);
            mnc_do_slabs_move(it_free, it_des, id);
            item_unlock(hv);
            mnc_remove_item(it_free);

            pthread_mutex_lock(&slab_lock);
            mnc_do_slabs_destroy(it_free, id);
            pthread_mutex_unlock(&slab_lock);
        }
        else
        {
            SYS_ABORT("ERROR it_flags %x !!!!!", it_free->it_flags);
        }

    }

    st_d_print("GOOD, Free Block Page: %p Size:%d ", ptr_free,
           (p_class->size * p_class->perslab) );
    pthread_mutex_lock(&slab_lock);
    free(ptr_free);
    p_class->slab_list[p_class->slabs-1] = NULL;
    -- p_class->slabs;
    mem_allocated -= p_class->size * p_class->perslab;
    pthread_mutex_unlock(&slab_lock);

    st_d_print("total memory: %lu, already used memory:%lu",
                    minicached_mem_limit, mem_allocated);

    return RET_YES;
}


static RET_T mnc_slabs_rebalance(unsigned int id)
{
    signed int i = 0;
    signed int j = 0;

    slabclass_t *p_class_free = NULL;
    mnc_item* it_free = NULL;

    // 从大块向小块整理释放，因为越是前面的小块，对象就越多
    // 整理释放的代价就越大
    for (i=SLAB_SZ_TYPE-1; i>=0; --i)
    {
        if (i == id)
            continue;

        if(mnc_do_slabs_recycle(i, 3.0) == RET_YES)
            return RET_YES;
    }

    for (i=SLAB_SZ_TYPE-1; i>=0; --i)
    {
        if (i == id)
            continue;

        if(mnc_do_slabs_recycle(i, 1.0) == RET_YES)
            return RET_YES;
    }

    return RET_NO;
}




/**
 * 清理函数，清除所有的内存缓存
 */
extern void mnc_mem_cleanup(void)
{
    signed int id = 0;
    unsigned int i=0, j=0;
    uint32_t hv = 0;

    slabclass_t *p_class = NULL;
    mnc_item* it_free = NULL;
    void* ptr_free = NULL;

    for (id=SLAB_SZ_TYPE-1; id>=0; --id)
    {
        p_class = &mnc_slabclass[id];
        unsigned int cur_slab_num = p_class->slabs;

        for (i=0; i<cur_slab_num; ++i)
        {
            ptr_free = p_class->slab_list[i];
            for (j=0; j<p_class->perslab; ++j)
            {
                it_free = (mnc_item *) ((char *)ptr_free + j*p_class->size);
                if (it_free->it_flags & ITEM_SLABBED)
                {
                    pthread_mutex_lock(&slab_lock);
                    mnc_do_slabs_destroy(it_free, id);
                    pthread_mutex_unlock(&slab_lock);
                }
                else if (it_free->it_flags & ITEM_PENDING)
                {
                    st_d_print("WARN!!!!>>> PENDING FREE....");

                    mnc_remove_item(it_free);
                    pthread_mutex_lock(&slab_lock);
                    mnc_do_slabs_destroy(it_free, id);
                    pthread_mutex_unlock(&slab_lock);
                }
                else if (it_free->it_flags & ITEM_LINKED)
                {
                    hv = hash(ITEM_key(it_free), it_free->nkey);

                    mnc_unlink_item_l(it_free);
                    mnc_remove_item(it_free);

                    pthread_mutex_lock(&slab_lock);
                    mnc_do_slabs_destroy(it_free, id);
                    pthread_mutex_unlock(&slab_lock);
                }
                else
                {
                    SYS_ABORT("ERROR it_flags %x !!!!!", it_free->it_flags);
                }
            }

            st_d_print("GOOD, Free Block Page: %p Size:%d ", ptr_free,
                       (p_class->size * p_class->perslab) );
            pthread_mutex_lock(&slab_lock);
            free(ptr_free);
            p_class->slab_list[i] = NULL;
            -- p_class->slabs;
            mem_allocated -= p_class->size * p_class->perslab;
            pthread_mutex_unlock(&slab_lock);

        }
    }

    st_d_print("total memory: %lu, already used memory:%lu",
                            minicached_mem_limit, mem_allocated);

    return;
}



void mnc_class_statistic(unsigned int id)
{
    slabclass_t* p_class = &mnc_slabclass[id];

    st_d_print("=========================================");
    st_d_print("class_id: %u", id);
    st_d_print("item size: %x", p_class->size);
    st_d_print("perslab count: %u", p_class->perslab);
    st_d_print("free count: %u", p_class->sl_curr);
    st_d_print("alloc slabs count: %u", p_class->slabs);
    st_d_print("slab list ptr count: %u", p_class->slab_list_size);
    st_d_print("requested bytes: %luKB", p_class->requested/1024);
    st_d_print("total memory: %luKB, already used memory:%luKB",
                            minicached_mem_limit/1024, mem_allocated/1024);
    st_d_print("");
    st_d_print("total request count: %lu", mnc_status.request_cnt);
    st_d_print("total cache hit count: %lu", mnc_status.hit_cnt);
    st_d_print("=========================================");

    return;
}


void mnc_general_statistic(void)
{
    time_t diff = mnc_get_current_time() - mnc_get_start_time();

    st_d_print("=========================================");
    st_d_print("total memory: %luKB, already used memory:%luKB",
                            minicached_mem_limit/1024, mem_allocated/1024);
    st_d_print("total request count: %lu", mnc_status.request_cnt);
    st_d_print("total cache hit count: %lu", mnc_status.hit_cnt);
    st_d_print("library serve time: %ld days, %ld hours, %ld mins ", diff/(24*60*60),
              (diff%(24*60*60))/(60*60),  (diff%(60*60))/(60));
    st_d_print("=========================================");

    return;
}
