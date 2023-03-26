
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


static ngx_inline void *ngx_palloc_small(ngx_pool_t *pool, size_t size,
    ngx_uint_t align);
static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


/*创建的内存池被结构体 ngx_pool_t 占去开头一部分（即额外的开销 overhead），Nginx实际是从该内存池里 p->d.last 指向的
起始位置开始分配给用户的*/
ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t  *p;

    /* 进行16字节的内存对齐分配 */
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);
    if (p == NULL) {
        return NULL;
    }

    p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    p->d.end = (u_char *) p + size;
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t);
    /* max的最大值为4095，假设一页大小为4KB.
     * 问：为什么要将pool->max字段的最大值限制在一页内存？
     * 这个字段是区分小块内存和大块内存的临界，所以这里的原因也就在于只有当
     * 分配的内存空间小于一页时才有缓存的必要（即向Nginx内存池申请），否则的
     * 话，还不如直接利用系统接口malloc()向操作系统申请。
     */
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}


void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

#if (NGX_DEBUG)

    /*
     * we could allocate the pool->log from this pool
     * so we cannot use this log while free()ing the pool
     */

    for (l = pool->large; l; l = l->next) {
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);
    }

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                       "free: %p, unused: %uz", p, p->d.end - p->d.last);

        if (n == NULL) {
            break;
        }
    }

#endif

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);

        if (n == NULL) {
            break;
        }
    }
}


void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }

    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
        p->d.failed = 0;
    }

    pool->current = pool;
    pool->chain = NULL;
    pool->large = NULL;
}


/*该函数尝试从 pool 内存池里分配 size 大小的内存空间。有两种情况：

如果 size 小于等于 pool->max (称之为小块内存分配)，即小于等于内存池总大小或 1 页内存(4K - 1)，则调用
ngx_palloc_small() 进行分配；
否则为大块内存分配， 即调用 ngx_palloc_large() 进行分配。*/
void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
#if !(NGX_DEBUG_PALLOC)
    if (size <= pool->max) {
        /* 第 3 个参数为 1 表示需要进行内存对齐 */
        return ngx_palloc_small(pool, size, 1);
    }
#endif

    return ngx_palloc_large(pool, size);
}


/*该函数与 ngx_palloc() 类似，只不过是没有进行内存对齐.*/
void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
#if !(NGX_DEBUG_PALLOC)
    if (size <= pool->max) {
        return ngx_palloc_small(pool, size, 0);
    }
#endif

    return ngx_palloc_large(pool, size);
}


/*小块内存分配*/
static ngx_inline void *
ngx_palloc_small(ngx_pool_t *pool, size_t size, ngx_uint_t align)
{
    u_char      *m;
    ngx_pool_t  *p;

    /* current 指向当前正在使用的 ngx_pool_t 内存池首地址 */
    p = pool->current;

    do {
        m = p->d.last;

        /* 进行内存对齐 */
        if (align) {
            m = ngx_align_ptr(m, NGX_ALIGNMENT);
        }

        /* 若当前内存池的可用空间充足，则直接从当前内存池中分配 size 大小空间 */
        if ((size_t) (p->d.end - m) >= size) {
            p->d.last = m + size;

            return m;
        }

        /* 若当前内存池的可用空间不足 size，则指向下一个内存池节点 */
        p = p->d.next;

    } while (p);

    /* 若遍历完 p->d.next 所链接的内存池链表都没有足够的内存空间，则调用 ngx_palloc_block()
     * 再分配一个等同大小的内存池，并将其链接到 p->d.next 内存池链表中 */
    return ngx_palloc_block(pool, size);
}


/*分配新的内存池并链接到内存池链表尾部*/
static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new;

    /* 获取 pool 指向的内存池总大小，其中包括内存池开头占用的 sizeof(ngx_pool_t) 大小的 overhead  */
    psize = (size_t) (pool->d.end - (u_char *) pool);

    /* 分配 psize 大小并 16 字节对齐的内存空间 */
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);
    if (m == NULL) {
        return NULL;
    }

    /* ngx_pool_t 类型的结构体指针 new 指向该新分配的内存起始地址 */
    new = (ngx_pool_t *) m;

    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;

    /* 该新分配的内存池的起始为 sizeof(ngx_pool_data_t) 大小的 overhead */
    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    /* 表示从新内存池中分配 size 大小的内存空间 */
    new->d.last = m + size;

    /* 遍历 pool->current 指向的内存池链表，根据 p->d.failed 值更新 pool->current */
    for (p = pool->current; p->d.next; p = p->d.next) {
        /* current 字段的变动是根据统计来做的，如果从当前内存池节点分配内存总失败次数(记录在
         * 字段 p->d.failed 内) 大于等于 6 次(这是一个经验值，具体判断是 "if (p->d.failed++ > 4)"
         * 由于 p->d.failed 初始值为 0，所以当这个判断为真时，至少已经分配失败 6 次了)，就将
         * current 字段移到下一个内存池节点，如果下一个内存池节点的 failed 统计数也大于等于 6 次，
         * 再下一个，依次类推，如果直到最后仍然是 failed 统计数大于等于 6 次，那么 current 字段
         * 指向刚新分配的内存池节点 */
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }

    /* 将新分配的内存池节点添加到内存池链表的尾部 */
    p->d.next = new;

    return m;
}


/*大块内存分配*/
static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;

    /* 直接malloc size 大小的内存 */
    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    /* 遍历 large 指向的大块内存链表 */
    for (large = pool->large; large; large = large->next) {
        /* 在内存池的使用过程中，由于大块内存可能会被释放(通过函数 ngx_pfree())，
         * 此时将空出其对应的头结构体变量 ngx_pool_large_t，所以在进行实际的链
         * 头插入操作前，会去搜索当前是否有这种情况存在。如果有，则直接将新分配的
         * 内存块设置在其 alloc 指针字段下。又如下可知，这种搜索也只是对前面 5 个
         * 链表节点进行 */
        if (large->alloc == NULL) {
            large->alloc = p;
            return p;
        }

        /* 这里表示仅搜索大块内存链表中前面的 5 个节点 */
        if (n++ > 3) {
            break;
        }
    }

    /* 若 ngx_pool_large_t 类型的指针 large 开始时为 NULL，则从内存池 pool 中
     * 分配一块 sizeof(ngx_pool_large_t) 大小的内存，并使 large 指向该内存起始 */
    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    /* alloc 指向上面直接 malloc 的大块内存 */
    large->alloc = p;
    /* 将该新分配的大块内存节点 large 插入到 pool->large 指向的大块内存链表头部 */
    large->next = pool->large;
    pool->large = large;

    return p;
}


void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    /* 分配一块 size 大小并进行 alignment 字节对齐的内存空间 */
    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    /* 从内存池 pool 中分配一块 sizeof(ngx_pool_large_t) 大小的内存空间 */
    large = ngx_palloc_small(pool, sizeof(ngx_pool_large_t), 1);
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    /* 将 p 指向的新分配的大块内存挂到 large->alloc 下 */
    large->alloc = p;
    /* 然后将 large 添加到 pool->large 指向的大块内存链表头 */
    large->next = pool->large;
    pool->large = large;

    return p;
}


ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;

    /* 遍历 pool->large 指向的大块内存链表，在该链表中找到 p 指向的大块内存，
     * 然后释放 p 指向的内存空间，并将 alloc 置为 NULL*/
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    /* 没有找到则返回 NGX_DECLINED */
    return NGX_DECLINED;
    /*Nginx 此外提供的一套函数接口 ngx_pool_cleanup_add()、ngx_pool_run_cleanup_file()、ngx_pool_cleanup_file()、
    ngx_pool_delete_file() 用于对内存与其他资源的关联管理，也就是说，从内存池里申请一块内存时，可能外部会
    附加一些其他资源（比如打开的文件），这些资源的使用和申请的内存是绑定在一起的，那么在进行资源释放的时候，
    希望这些资源的释放能和内存池释放一起进行（通过 handler() 回调函数），既能避免无意的资源泄漏，又省得单独
    执行资源释放的麻烦。*/
}


void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;

    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }

    if (size) {
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

    c->handler = NULL;
    c->next = p->cleanup;

    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


void
ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;
    ngx_pool_cleanup_file_t  *cf;

    for (c = p->cleanup; c; c = c->next) {
        if (c->handler == ngx_pool_cleanup_file) {

            cf = c->data;

            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}


void
ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


void
ngx_pool_delete_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);

    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
