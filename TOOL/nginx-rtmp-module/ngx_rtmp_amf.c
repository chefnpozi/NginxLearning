
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_amf.h"
#include "ngx_rtmp.h"
#include <string.h>


static ngx_inline void*
ngx_rtmp_amf_reverse_copy(void *dst, void* src, size_t len)
{
    size_t  k;

    if (dst == NULL || src == NULL) {
        return NULL;
    }

    /* 将 src 中的数据从末尾开始一个个赋给 dst[0],dst[1],dst[2],... */
    for(k = 0; k < len; ++k) {
        ((u_char*)dst)[k] = ((u_char*)src)[len - 1 - k];
    }

    return dst;
}

#define NGX_RTMP_AMF_DEBUG_SIZE 16

#ifdef NGX_DEBUG
static void
ngx_rtmp_amf_debug(const char* op, ngx_log_t *log, u_char *p, size_t n)
{
    u_char          hstr[3 * NGX_RTMP_AMF_DEBUG_SIZE + 1];
    u_char          str[NGX_RTMP_AMF_DEBUG_SIZE + 1];
    u_char         *hp, *sp;
    static u_char   hex[] = "0123456789ABCDEF";
    size_t          i;

    hp = hstr;
    sp = str;

    for(i = 0; i < n && i < NGX_RTMP_AMF_DEBUG_SIZE; ++i) {
        *hp++ = ' ';
        if (p) {
            *hp++ = hex[(*p & 0xf0) >> 4];
            *hp++ = hex[*p & 0x0f];
            *sp++ = (*p >= 0x20 && *p <= 0x7e) ?
                *p : (u_char)'?';
            ++p;
        } else {
            *hp++ = 'X';
            *hp++ = 'X';
            *sp++ = '?';
        }
    }
    *hp = *sp = '\0';

    ngx_log_debug4(NGX_LOG_DEBUG_RTMP, log, 0,
            "AMF %s (%d)%s '%s'", op, n, hstr, str);
}
#endif

/* @ctx：指向 ngx_rtmp_amf_ctx_t 类型的指针，该结构体的成员 link 保存着接收到的 amf 数据
 * @p：将读取到的数据保存到该指针 p 指向的内存
 * @n：要读取的字节数
 */
static ngx_int_t
ngx_rtmp_amf_get(ngx_rtmp_amf_ctx_t *ctx, void *p, size_t n)
{
    size_t          size;
    ngx_chain_t    *l;
    size_t          offset;
    u_char         *pos, *last;
#ifdef NGX_DEBUG
    void           *op = p;
    size_t          on = n;
#endif

    if (!n)
        return NGX_OK;

    for(l = ctx->link, offset = ctx->offset; l; l = l->next, offset = 0) {

        /* 开始 pos 指向所要读取数据的起始地址 */
        pos  = l->buf->pos + offset;
        /* last 指向数据的末尾地址 */
        last = l->buf->last;

        /* 当数据充足时 */
        if (last >= pos + n) {
            if (p) {
                /* 拷贝 n 个字节的数据到 p 指向的内存中 */
                p = ngx_cpymem(p, pos, n);
            }
            /* ctx->offset 的偏移值加 n，表示已经读出了 n 个字节的数据 */
            ctx->offset = offset + n;
            ctx->link = l;

#ifdef NGX_DEBUG
            ngx_rtmp_amf_debug("read", ctx->log, (u_char*)op, on);
#endif

            return NGX_OK;
        }

        /* 当 ctx->link 指向的 ngx_chain_t 链表保存的数据不足 n 个时，则将能读取出的数据都读取出来 */
        size = last - pos;

        if (p) {
            p = ngx_cpymem(p, pos, size);
        }

        /* 计算余下未读的字节数，跳到下一个链表继续读取，直到读够为止 */
        n -= size;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, ctx->log, 0,
            "AMF read eof (%d)", n);

    return NGX_DONE;
}


static ngx_int_t
ngx_rtmp_amf_put(ngx_rtmp_amf_ctx_t *ctx, void *p, size_t n)
{
    ngx_buf_t       *b;
    size_t          size;
    ngx_chain_t    *l, *ln;

#ifdef NGX_DEBUG
    ngx_rtmp_amf_debug("write", ctx->log, (u_char*)p, n);
#endif

    l = ctx->link;

    if (ctx->link && ctx->first == NULL) {
        ctx->first = ctx->link;
    }

    while(n) {
        b = l ? l->buf : NULL;

        if (b == NULL || b->last == b->end) {

            ln = ctx->alloc(ctx->arg);
            if (ln == NULL) {
                return NGX_ERROR;
            }

            if (ctx->first == NULL) {
                ctx->first = ln;
            }

            if (l) {
                l->next = ln;
            }

            l = ln;
            ctx->link = l;
            b = l->buf;
        }

        size = b->end - b->last;

        /* 若要写入的缓存空间足够，则直接将其cp */
        if (size >= n) {
            b->last = ngx_cpymem(b->last, p, n);
            return NGX_OK;
        }

        /* 否则，若内存不足，则只写入 size 字节的数组，
         * 余下的写入下一个 ngx_chain_t 中(存在的话) */
        b->last = ngx_cpymem(b->last, p, size);
        p = (u_char*)p + size;
        n -= size;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_amf_read_object(ngx_rtmp_amf_ctx_t *ctx, ngx_rtmp_amf_elt_t *elts,
        size_t nelts)
{
    uint8_t                 type;
    uint16_t                len;
    size_t                  n, namelen, maxlen;
    ngx_int_t               rc;
    u_char                  buf[2];

    maxlen = 0;
    for(n = 0; n < nelts; ++n) {
        namelen = elts[n].name.len;
        if (namelen > maxlen)
            maxlen = namelen;
    }

    for( ;; ) {

#if !(NGX_WIN32)
        char    name[maxlen];
#else
        char    name[1024];
        if (maxlen > sizeof(name)) {
            return NGX_ERROR;
        }
#endif
        /* read key */
        switch (ngx_rtmp_amf_get(ctx, buf, 2)) {
        case NGX_DONE:
            /* Envivio sends unfinalized arrays */
            return NGX_OK;
        case NGX_OK:
            break;
        default:
            return NGX_ERROR;
        }

        ngx_rtmp_amf_reverse_copy(&len, buf, 2);

        if (!len)
            break;

        if (len <= maxlen) {
            rc = ngx_rtmp_amf_get(ctx, name, len);

        } else {
            rc = ngx_rtmp_amf_get(ctx, name, maxlen);
            if (rc != NGX_OK)
                return NGX_ERROR;
            rc = ngx_rtmp_amf_get(ctx, 0, len - maxlen);
        }

        if (rc != NGX_OK)
            return NGX_ERROR;

        /* TODO: if we require array to be sorted on name
         * then we could be able to use binary search */
        for(n = 0; n < nelts
                && (len != elts[n].name.len
                    || ngx_strncmp(name, elts[n].name.data, len));
                ++n);

        if (ngx_rtmp_amf_read(ctx, n < nelts ? &elts[n] : NULL, 1) != NGX_OK)
            return NGX_ERROR;
    }

    if (ngx_rtmp_amf_get(ctx, &type, 1) != NGX_OK
        || type != NGX_RTMP_AMF_END)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_amf_read_array(ngx_rtmp_amf_ctx_t *ctx, ngx_rtmp_amf_elt_t *elts,
        size_t nelts)
{
    uint32_t                len;
    size_t                  n;
    u_char                  buf[4];

    /* read length */
    if (ngx_rtmp_amf_get(ctx, buf, 4) != NGX_OK)
        return NGX_ERROR;

    ngx_rtmp_amf_reverse_copy(&len, buf, 4);

    for (n = 0; n < len; ++n) {
        if (ngx_rtmp_amf_read(ctx, n < nelts ? &elts[n] : NULL, 1) != NGX_OK)
            return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_amf_read_variant(ngx_rtmp_amf_ctx_t *ctx, ngx_rtmp_amf_elt_t *elts,
        size_t nelts)
{
    uint8_t                 type;
    ngx_int_t               rc;
    size_t                  n;
    ngx_rtmp_amf_elt_t      elt;

    rc = ngx_rtmp_amf_get(ctx, &type, 1);
    if (rc != NGX_OK) {
        return rc;
    }

    ngx_memzero(&elt, sizeof(elt));
    for (n = 0; n < nelts; ++n, ++elts) {
        if (type == elts->type) {
            elt.data = elts->data;
            elt.len  = elts->len;
        }
    }

    elt.type = type | NGX_RTMP_AMF_TYPELESS;

    return ngx_rtmp_amf_read(ctx, &elt, 1);
}


static ngx_int_t
ngx_rtmp_amf_is_compatible_type(uint8_t t1, uint8_t t2)
{
    return t1 == t2
        || (t1 == NGX_RTMP_AMF_OBJECT && t2 == NGX_RTMP_AMF_MIXED_ARRAY)
        || (t2 == NGX_RTMP_AMF_OBJECT && t1 == NGX_RTMP_AMF_MIXED_ARRAY);
}


ngx_int_t
ngx_rtmp_amf_read(ngx_rtmp_amf_ctx_t *ctx, ngx_rtmp_amf_elt_t *elts,
        size_t nelts)
{
    void                       *data;
    ngx_int_t                   type;
    uint8_t                     type8;
    size_t                      n;
    uint16_t                    len;
    ngx_int_t                   rc;
    u_char                      buf[8];
    uint32_t                    max_index;

    for(n = 0; n < nelts; ++n) {

        /* 一般 amf 命令名通常伴随着字符串类型，但是 shared object 名是没有类型的 */
        if (elts && elts->type & NGX_RTMP_AMF_TYPELESS) {
            type = elts->type & ~NGX_RTMP_AMF_TYPELESS;
            data = elts->data;

        } else {
            /* 读取 1 个字节的数据到 type8 中 */
            switch (ngx_rtmp_amf_get(ctx, &type8, 1)) {
                case NGX_DONE:
                    if (elts && elts->type & NGX_RTMP_AMF_OPTIONAL) {
                        return NGX_OK;
                    }
                    /* fall through */
                case NGX_ERROR:
                    return NGX_ERROR;
            }
            type = type8;
            data = (elts &&
                    /* 检测 elts->type 的类型 与 读取出的类型 type 是否相同，是则 
                     * data 指向 elts->data，否则为 NULL */
                    ngx_rtmp_amf_is_compatible_type(
                                 (uint8_t) (elts->type & 0xff), (uint8_t) type))
                ? elts->data
                : NULL;

            if (elts && (elts->type & NGX_RTMP_AMF_CONTEXT)) {
                if (data) {
                    *(ngx_rtmp_amf_ctx_t *) data = *ctx;
                }
                data = NULL;
            }
        }

        /* 根据类型 type 取出对应的数据 */
        switch (type) {
            case NGX_RTMP_AMF_NUMBER:
                if (ngx_rtmp_amf_get(ctx, buf, 8) != NGX_OK) {
                    return NGX_ERROR;
                }
                ngx_rtmp_amf_reverse_copy(data, buf, 8);
                break;

            case NGX_RTMP_AMF_BOOLEAN:
                if (ngx_rtmp_amf_get(ctx, data, 1) != NGX_OK) {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_STRING:
                /* 读取 2 字节的 length，保存到 buf 中 */
                if (ngx_rtmp_amf_get(ctx, buf, 2) != NGX_OK) {
                    return NGX_ERROR;
                }
                /* 将 buf 中保存的大端字节序转换为小端字节序，保存到 len 中 */
                ngx_rtmp_amf_reverse_copy(&len, buf, 2);

                if (data == NULL) {
                    rc = ngx_rtmp_amf_get(ctx, data, len);

                } else if (elts->len <= len) {
                    rc = ngx_rtmp_amf_get(ctx, data, elts->len - 1);
                    if (rc != NGX_OK)
                        return NGX_ERROR;
                    ((char*)data)[elts->len - 1] = 0;
                    rc = ngx_rtmp_amf_get(ctx, NULL, len - elts->len + 1);

                } else {
                    /* 读取该 amf 字符串的真正的字符串数据到 data 中 */
                    rc = ngx_rtmp_amf_get(ctx, data, len);
                    ((char*)data)[len] = 0;
                }

                if (rc != NGX_OK) {
                    return NGX_ERROR;
                }

                break;

            case NGX_RTMP_AMF_NULL:
            case NGX_RTMP_AMF_ARRAY_NULL:
                break;

            case NGX_RTMP_AMF_MIXED_ARRAY:
                if (ngx_rtmp_amf_get(ctx, &max_index, 4) != NGX_OK) {
                    return NGX_ERROR;
                }
                /* fall through */

            case NGX_RTMP_AMF_OBJECT:
                if (ngx_rtmp_amf_read_object(ctx, data,
                    data && elts ? elts->len / sizeof(ngx_rtmp_amf_elt_t) : 0
                    ) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_ARRAY:
                if (ngx_rtmp_amf_read_array(ctx, data,
                    data && elts ? elts->len / sizeof(ngx_rtmp_amf_elt_t) : 0
                    ) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_VARIANT_:
                if (ngx_rtmp_amf_read_variant(ctx, data,
                    data && elts ? elts->len / sizeof(ngx_rtmp_amf_elt_t) : 0
                    ) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_INT8:
                if (ngx_rtmp_amf_get(ctx, data, 1) != NGX_OK) {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_INT16:
                if (ngx_rtmp_amf_get(ctx, buf, 2) != NGX_OK) {
                    return NGX_ERROR;
                }
                ngx_rtmp_amf_reverse_copy(data, buf, 2);
                break;

            case NGX_RTMP_AMF_INT32:
                if (ngx_rtmp_amf_get(ctx, buf, 4) != NGX_OK) {
                    return NGX_ERROR;
                }
                ngx_rtmp_amf_reverse_copy(data, buf, 4);
                break;

            case NGX_RTMP_AMF_END:
                return NGX_OK;

            default:
                return NGX_ERROR;
        }

        if (elts) {
            /* 若 elts 数组中的当前元素获取成功，则接着获取下一个 elts 元素要获取的值 */
            ++elts;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_amf_write_object(ngx_rtmp_amf_ctx_t *ctx,
        ngx_rtmp_amf_elt_t *elts, size_t nelts)
{
    uint16_t                len;
    size_t                  n;
    u_char                  buf[2];

    for(n = 0; n < nelts; ++n) {

        len = (uint16_t) elts[n].name.len;

        /* 先写入 2 bytes 长度值 */
        if (ngx_rtmp_amf_put(ctx,
                    ngx_rtmp_amf_reverse_copy(buf,
                        &len, 2), 2) != NGX_OK)
        {
            return NGX_ERROR;
        }

        /* 接着将 len bytes 的 名称写入*/
        if (ngx_rtmp_amf_put(ctx, elts[n].name.data, len) != NGX_OK) {
            return NGX_ERROR;
        }

        /* 接着写入上面 elts[n].name 的值 */
        if (ngx_rtmp_amf_write(ctx, &elts[n], 1) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    /* 该 object 的项都写入完成后，最后写入 2 bytes 的 '\0' */
    if (ngx_rtmp_amf_put(ctx, "\0\0", 2) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_amf_write_array(ngx_rtmp_amf_ctx_t *ctx,
        ngx_rtmp_amf_elt_t *elts, size_t nelts)
{
    uint32_t                len;
    size_t                  n;
    u_char                  buf[4];

    len = nelts;
    if (ngx_rtmp_amf_put(ctx,
                ngx_rtmp_amf_reverse_copy(buf,
                    &len, 4), 4) != NGX_OK)
    {
        return NGX_ERROR;
    }

    for(n = 0; n < nelts; ++n) {
        if (ngx_rtmp_amf_write(ctx, &elts[n], 1) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_amf_write(ngx_rtmp_amf_ctx_t *ctx,
        ngx_rtmp_amf_elt_t *elts, size_t nelts)
{
    size_t                  n;
    ngx_int_t               type;
    uint8_t                 type8;
    void                   *data;
    uint16_t                len;
    uint32_t                max_index;
    u_char                  buf[8];

    for(n = 0; n < nelts; ++n) {

        type = elts[n].type;
        data = elts[n].data;
        len  = (uint16_t) elts[n].len;

        /* 如果该 amf 为 shared 类型，则表示没有该 amf 数据
         * 不是 "类型+值" 的形式，即直接是 "值" */
        if (type & NGX_RTMP_AMF_TYPELESS) {
            type &= ~NGX_RTMP_AMF_TYPELESS;
        } else {
            type8 = (uint8_t)type;
            /* 先写入 1 byte 的类型 */
            if (ngx_rtmp_amf_put(ctx, &type8, 1) != NGX_OK)
                return NGX_ERROR;
        }

        switch(type) {
            case NGX_RTMP_AMF_NUMBER:
                if (ngx_rtmp_amf_put(ctx,
                            ngx_rtmp_amf_reverse_copy(buf,
                                data, 8), 8) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_BOOLEAN:
                if (ngx_rtmp_amf_put(ctx, data, 1) != NGX_OK) {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_STRING:
                if (len == 0 && data) {
                    len = (uint16_t) ngx_strlen((u_char*) data);
                }

                /* 写入 2 bytes 的 长度 */
                if (ngx_rtmp_amf_put(ctx,
                            ngx_rtmp_amf_reverse_copy(buf,
                                &len, 2), 2) != NGX_OK)
                {
                    return NGX_ERROR;
                }

                /* 接着写入 len 大小的字符串数据 data */
                if (ngx_rtmp_amf_put(ctx, data, len) != NGX_OK) {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_NULL:
            case NGX_RTMP_AMF_ARRAY_NULL:
                break;

            case NGX_RTMP_AMF_MIXED_ARRAY:
                max_index = 0;
                if (ngx_rtmp_amf_put(ctx, &max_index, 4) != NGX_OK) {
                    return NGX_ERROR;
                }
                /* fall through */

            case NGX_RTMP_AMF_OBJECT:
                type8 = NGX_RTMP_AMF_END;
                if (ngx_rtmp_amf_write_object(ctx, data,
                        elts[n].len / sizeof(ngx_rtmp_amf_elt_t)) != NGX_OK
                    || ngx_rtmp_amf_put(ctx, &type8, 1) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_ARRAY:
                if (ngx_rtmp_amf_write_array(ctx, data,
                        elts[n].len / sizeof(ngx_rtmp_amf_elt_t)) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_INT8:
                if (ngx_rtmp_amf_put(ctx, data, 1) != NGX_OK) {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_INT16:
                if (ngx_rtmp_amf_put(ctx,
                            ngx_rtmp_amf_reverse_copy(buf,
                                data, 2), 2) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            case NGX_RTMP_AMF_INT32:
                if (ngx_rtmp_amf_put(ctx,
                            ngx_rtmp_amf_reverse_copy(buf,
                                data, 4), 4) != NGX_OK)
                {
                    return NGX_ERROR;
                }
                break;

            default:
                return NGX_ERROR;
        }
    }

    return NGX_OK;
}

