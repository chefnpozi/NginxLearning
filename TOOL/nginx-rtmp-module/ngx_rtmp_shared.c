
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp.h"


// 分配一个 shared 的 ngx_chain_t 结构体内存
ngx_chain_t *
ngx_rtmp_alloc_shared_buf(ngx_rtmp_core_srv_conf_t *cscf)
{
    u_char                     *p;
    ngx_chain_t                *out;
    ngx_buf_t                  *b;
    size_t                      size;

    /* 若 free 有空闲的 ngx_chain_t，则直接从 free 指向的链表头中取 */
    if (cscf->free) {
        out = cscf->free;
        cscf->free = out->next;

    } else {

        /* 计算一个实际的 rtmp 块的最大值 */
        size = cscf->chunk_size + NGX_RTMP_MAX_CHUNK_HEADER;

        /* 从内存池中分配一块连续的内存 */
        p = ngx_pcalloc(cscf->pool, NGX_RTMP_REFCOUNT_BYTES
                + sizeof(ngx_chain_t)
                + sizeof(ngx_buf_t)
                + size);
        if (p == NULL) {
            return NULL;
        }

        /* 这块内存的开始 4 bytes 是用于保存这块 shared 内存的引用计数值的 */
        p += NGX_RTMP_REFCOUNT_BYTES;
        out = (ngx_chain_t *)p;

        p += sizeof(ngx_chain_t);
        out->buf = (ngx_buf_t *)p;

        p += sizeof(ngx_buf_t);
        out->buf->start = p;
        out->buf->end = p + size;
    }

    out->next = NULL;
    b = out->buf;
    /* 由于分配好内存后，下一步操作是直接向这块内存写入 rtmp 的 chunk data，
     * 即跳过了 rtmp 的头部，暂时先写入实际数据，因此，这里需要为 rtmp 的头部
     * 预留足够的内存，这里为NGX_RTMP_MAX_CHUNK_HEADER */
    b->pos = b->last = b->start + NGX_RTMP_MAX_CHUNK_HEADER;
    b->memory = 1;

    /* 这里引用计数最初置为 1 */
    /* buffer has refcount =1 when created! */
    ngx_rtmp_ref_set(out, 1);

    return out;
}


void
ngx_rtmp_free_shared_chain(ngx_rtmp_core_srv_conf_t *cscf, ngx_chain_t *in)
{
    ngx_chain_t        *cl;

    /* 引用计数减 1 */
    if (ngx_rtmp_ref_put(in)) {
        return;
    }

    /* 将 cl 插入到 cscf->free 链表头中 */
    for (cl = in; ; cl = cl->next) {
        if (cl->next == NULL) {
            cl->next = cscf->free;
            cscf->free = in;
            return;
        }
    }
}


ngx_chain_t *
ngx_rtmp_append_shared_bufs(ngx_rtmp_core_srv_conf_t *cscf,
        ngx_chain_t *head, ngx_chain_t *in)
{
    ngx_chain_t                    *l, **ll;
    u_char                         *p;
    size_t                          size;

    ll = &head;
    p = in->buf->pos;
    l = head;

    if (l) {
        for(; l->next; l = l->next);
        ll = &l->next;
    }

    for ( ;; ) {

        if (l == NULL || l->buf->last == l->buf->end) {
            l = ngx_rtmp_alloc_shared_buf(cscf);
            if (l == NULL || l->buf == NULL) {
                break;
            }

            *ll = l;
            ll = &l->next;
        }

        while (l->buf->end - l->buf->last >= in->buf->last - p) {
            l->buf->last = ngx_cpymem(l->buf->last, p,
                    in->buf->last - p);
            in = in->next;
            if (in == NULL) {
                goto done;
            }
            p = in->buf->pos;
        }

        size = l->buf->end - l->buf->last;
        l->buf->last = ngx_cpymem(l->buf->last, p, size);
        p += size;
    }

done:
    *ll = NULL;

    return head;
}
