
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>


static void ngx_rtmp_handshake_send(ngx_event_t *wev);
static void ngx_rtmp_handshake_recv(ngx_event_t *rev);
static void ngx_rtmp_handshake_done(ngx_rtmp_session_t *s);


/* RTMP handshake :
 *
 *          =peer1=                      =peer2=
 * challenge ----> (.....[digest1]......) ----> 1537 bytes
 * response  <---- (...........[digest2]) <---- 1536 bytes
 *
 *
 * - both packets contain random bytes except for digests
 * - digest1 position is calculated on random packet bytes
 * - digest2 is always at the end of the packet
 *
 * digest1: HMAC_SHA256(packet, peer1_partial_key)
 * digest2: HMAC_SHA256(packet, HMAC_SHA256(digest1, peer2_full_key))
 */


/* Handshake keys */
static u_char
ngx_rtmp_server_key[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'M', 'e', 'd', 'i', 'a', ' ',
    'S', 'e', 'r', 'v', 'e', 'r', ' ',
    '0', '0', '1',

    0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1,
    0x02, 0x9E, 0x7E, 0x57, 0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
    0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
};


static u_char
ngx_rtmp_client_key[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'P', 'l', 'a', 'y', 'e', 'r', ' ',
    '0', '0', '1',

    0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8, 0x2E, 0x00, 0xD0, 0xD1,
    0x02, 0x9E, 0x7E, 0x57, 0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
    0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
};


static const u_char
ngx_rtmp_server_version[4] = {
    0x0D, 0x0E, 0x0A, 0x0D
};


static const u_char
ngx_rtmp_client_version[4] = {
    0x0C, 0x00, 0x0D, 0x0E
};


#define NGX_RTMP_HANDSHAKE_KEYLEN                   SHA256_DIGEST_LENGTH
#define NGX_RTMP_HANDSHAKE_BUFSIZE                  1537


#define NGX_RTMP_HANDSHAKE_SERVER_RECV_CHALLENGE    1 // 1阶段，服务端接收客户端发送的 C0 C1
#define NGX_RTMP_HANDSHAKE_SERVER_SEND_CHALLENGE    2 // 2阶段，服务端发送给客户端 S0 S1
#define NGX_RTMP_HANDSHAKE_SERVER_SEND_RESPONSE     3 // 服务端发送 S2
#define NGX_RTMP_HANDSHAKE_SERVER_RECV_RESPONSE     4 // 服务端接收客户端发送的 C2
#define NGX_RTMP_HANDSHAKE_SERVER_DONE              5 // 接收到 C2 后，至此，服务器和客户端的 rtmp handshake 过程完整，开始正常的信息交互阶段
// ngx_rtmp_handshake_done 接收到 C2 后，服务器即进入循环处理客户端的请求阶段：ngx_rtmp_cycle

#define NGX_RTMP_HANDSHAKE_CLIENT_SEND_CHALLENGE    6
#define NGX_RTMP_HANDSHAKE_CLIENT_RECV_CHALLENGE    7
#define NGX_RTMP_HANDSHAKE_CLIENT_RECV_RESPONSE     8
#define NGX_RTMP_HANDSHAKE_CLIENT_SEND_RESPONSE     9
#define NGX_RTMP_HANDSHAKE_CLIENT_DONE              10


static ngx_str_t            ngx_rtmp_server_full_key
    = { sizeof(ngx_rtmp_server_key), ngx_rtmp_server_key };
static ngx_str_t            ngx_rtmp_server_partial_key
    = { 36, ngx_rtmp_server_key };

static ngx_str_t            ngx_rtmp_client_full_key
    = { sizeof(ngx_rtmp_client_key), ngx_rtmp_client_key };
static ngx_str_t            ngx_rtmp_client_partial_key
    = { 30, ngx_rtmp_client_key };


static ngx_int_t
ngx_rtmp_make_digest(ngx_str_t *key, ngx_buf_t *src,
        u_char *skip, u_char *dst, ngx_log_t *log)
{
    static HMAC_CTX        *hmac;
    unsigned int            len;

    if (hmac == NULL) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        static HMAC_CTX  shmac;
        hmac = &shmac;
        HMAC_CTX_init(hmac);
#else
        /* 初始化 hmac，在计算 MAC 之前必须调用此函数 */
        hmac = HMAC_CTX_new();
        if (hmac == NULL) {
            return NGX_ERROR;
        }
#endif
    }

    /* 初始化 hmac 数据，在 hmac 中复制 EVP_256() 方法，密钥数据，
     * 密钥数据长度len，最后一个参数为 NULL */
    HMAC_Init_ex(hmac, key->data, key->len, EVP_sha256(), NULL);

    if (skip && src->pos <= skip && skip <= src->last) {
        if (skip != src->pos) {
            /* 将数据块加入到计算结果中，数据缓冲区 src->pos，数据长度skip - src->pos */
            HMAC_Update(hmac, src->pos, skip - src->pos);
        }
        if (src->last != skip + NGX_RTMP_HANDSHAKE_KEYLEN) {
            HMAC_Update(hmac, skip + NGX_RTMP_HANDSHAKE_KEYLEN,
                    src->last - skip - NGX_RTMP_HANDSHAKE_KEYLEN);
        }
    } else {
        HMAC_Update(hmac, src->pos, src->last - src->pos);
    }

    /* 计算出的 MAC 数据放置在 dst 中，用户需要保证 dst 数据有足够长的空间，长度为 len */
    HMAC_Final(hmac, dst, &len);

    return NGX_OK;
    /*散列消息鉴别码，简称 HMAC，是一种基于消息鉴别码 MAC（Message Authentication Code）的鉴别机制。使用 HMAC 时，
    消息通讯的双方，通过验证消息中加入的鉴别密钥 K 来鉴别消息的真伪*/
}


/* 当 base 为 772 时，此时假设 C1 数据的结构如下：
 * - time: 4 bytes
 * - version: 4 bytes
 * - key: 764 bytes
 * - digest: 764 bytes
 * 此时查找 digest，base 为 772，即跳过 C1 数据的 time、version、keys 这前三部分共 772 字节的数据，
 * 从 digest 数据开始查找真正的 digest-data.
 *
 * 当 base 为 8 时，此时假设 C1 数据的结构如下：
 * - time: 4 bytes
 * - version: 4 bytes
 * - digest: 764 bytes
 * - key: 764 bytes
 * 此时查找 digest，base 为 8，即跳过 C1 数据的 time、version 这前两部分共 8 字节的数据，
 * 从 digest 数据开始查找真正的 digest-data.
 *
 * 764 bytes digest结构：
 * - offset: 4 bytes
 * - random-data: (offset) bytes
 * - digest-data: 32 bytes
 * - random-data: (764 - 4 - offset - 32 = 728 - offset) bytes
 *
 * 参考上面的digest结构，查找算法是：将 digest 数据开始的前 4 字节相加得到 offs，即 offset 值，
 * 然后将 offs 除以 728 后取余数，再加上 base + 4，得出 digest-data 的在 C1 数据中的偏移值
 * 
 */
static ngx_int_t
ngx_rtmp_find_digest(ngx_buf_t *b, ngx_str_t *key, size_t base, ngx_log_t *log)
{
    size_t                  n, offs;
    u_char                  digest[NGX_RTMP_HANDSHAKE_KEYLEN];
    u_char                 *p;

    offs = 0;
    /* 将 digest 数据开始的前 4 字节的值相加 得到 offset */
    for (n = 0; n < 4; ++n) {
        offs += b->pos[base + n];
    }
    offs = (offs % 728) + base + 4;
    /* p 指向 digest-data 的起始地址 */
    p = b->pos + offs;

    if (ngx_rtmp_make_digest(key, b, p, digest, log) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 校验计算出来的 digest 与 p 指向的 digest-data 是否相同，相同表示校验
     * 成功，即找到了正确的 digest-data 的偏移值 */
    if (ngx_memcmp(digest, p, NGX_RTMP_HANDSHAKE_KEYLEN) == 0) {
        return offs;
    }

    return NGX_ERROR;
}


/* 在 Nginx-rtmp 中 S1 的数据结构使用如下:
 * - time: 4 bytes
 * - version: 4 bytes
 * - digest: 764 bytes
 * - key: 764 bytes
 * 
 * 764 bytes digest 结构：
 * - offset: 4 bytes
 * - random-data: (offset) bytes
 * - digest-data: 32 bytes
 * - random-data: (764 - 4 - offset - 32 = 728 - offset) bytes
 */
static ngx_int_t
ngx_rtmp_write_digest(ngx_buf_t *b, ngx_str_t *key, size_t base,
        ngx_log_t *log)
{
    size_t                  n, offs;
    u_char                 *p;

    offs = 0;
    /* 将 b->pos[8] ~ b->pos[11] 这四个值相加得到 offset */
    for (n = 8; n < 12; ++n) {
        offs += b->pos[base + n];
    }
    offs = (offs % 728) + base + 12;
    p = b->pos + offs;

    /* 生成 digest 并存放在 p 指向的位置 */
    if (ngx_rtmp_make_digest(key, b, p, p, log) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_rtmp_fill_random_buffer(ngx_buf_t *b)
{
    for (; b->last != b->end; ++b->last) {
        *b->last = (u_char) rand();
    }
}


static ngx_buf_t *
ngx_rtmp_alloc_handshake_buffer(ngx_rtmp_session_t *s)
{
    ngx_rtmp_core_srv_conf_t   *cscf;
    ngx_chain_t                *cl;
    ngx_buf_t                  *b;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: allocating buffer");

    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    if (cscf->free_hs) {
        cl = cscf->free_hs;
        b = cl->buf;
        cscf->free_hs = cl->next;
        ngx_free_chain(cscf->pool, cl);

    } else {
        b = ngx_pcalloc(cscf->pool, sizeof(ngx_buf_t));
        if (b == NULL) {
            return NULL;
        }
        b->memory = 1;
        b->start = ngx_pcalloc(cscf->pool, NGX_RTMP_HANDSHAKE_BUFSIZE);
        if (b->start == NULL) {
            return NULL;
        }
        b->end = b->start + NGX_RTMP_HANDSHAKE_BUFSIZE;
    }

    b->pos = b->last = b->start;

    return b;
}


void
ngx_rtmp_free_handshake_buffers(ngx_rtmp_session_t *s)
{
    ngx_rtmp_core_srv_conf_t   *cscf;
    ngx_chain_t                *cl;

    if (s->hs_buf == NULL) {
        return;
    }
    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);
    cl = ngx_alloc_chain_link(cscf->pool);
    if (cl == NULL) {
        return;
    }
    cl->buf = s->hs_buf;
    cl->next = cscf->free_hs;
    cscf->free_hs = cl;
    s->hs_buf = NULL;
}


/*
 * RTMP complex handshake: S0+S1
 * 
 * 抓包知，S0 1 byte，S1和S2都为1536 bytes
 * 
 * S0: 1 byte，为rtmp版本号"\x03"
 *
 * S1: 1536 bytes (key和digest有些会调换一下)
 *    - time: 4 bytes
 *    - version: 4 bytes
 *    - key: 764 bytes
 *    - digest: 764 bytes
 *
 * 该函数仅构建了 S0+S1 共1537字节
 */
static ngx_int_t
ngx_rtmp_handshake_create_challenge(ngx_rtmp_session_t *s,
        const u_char version[4], ngx_str_t *key)
{
    ngx_buf_t          *b;

    b = s->hs_buf;
    b->last = b->pos = b->start;
    /* S0: 1 byte */
    *b->last++ = '\x03';
    /* S1 */
    /* - time: 4 bytes */
    b->last = ngx_rtmp_rcpymem(b->last, &s->epoch, 4);
    /* - version: 4 bytes */
    b->last = ngx_cpymem(b->last, version, 4);
    /* 之后的缓存先填满随机数据 */
    ngx_rtmp_fill_random_buffer(b);
    ++b->pos;   // 先略过 S0
    /* 写入服务器的 digest-data  */
    if (ngx_rtmp_write_digest(b, key, 0, s->connection->log) != NGX_OK) {
        return NGX_ERROR;
    }
    --b->pos;   // 再恢复到指向 S0 位置
    return NGX_OK;
}


/*
 * RTMP Complex handshake:
 *
 * 抓包知：C0+C1共1537 bytes，C0 1 byte，C1 1536 bytes.
 * 
 * C0: 1byte，即rtmp的版本号"\x03"
 * 
 * C1和S1包含两部分数据：key和digest，分别为如下：
 * - time: 4 bytes
 * - version: 4 bytes
 * - key: 764 bytes
 * - digest: 764 bytes
 * 
 * key和digest的顺序是不确定的，也有可能是:(nginx-rtmp中是如下的顺序)
 * - time: 4 bytes
 * - version: 4 bytes
 * - digest: 764 bytes
 * - key: 764 bytes

 * 
 * 764 bytes key 结构：
 * - random-data: (offset) bytes
 * - key-data: 128 bytes
 * - random-data: (764 - offset - 128 - 4) bytes
 * - offset: 4 bytes
 * 
 * 764 bytes digest结构：
 * - offset: 4 bytes
 * - random-data: (offset) bytes
 * - digest-data: 32 bytes
 * - random-data: (764 - 4 - offset - 32) bytes
 */
static ngx_int_t
ngx_rtmp_handshake_parse_challenge(ngx_rtmp_session_t *s,
        ngx_str_t *peer_key, ngx_str_t *key)
{
    ngx_buf_t              *b;
    u_char                 *p;
    ngx_int_t               offs;

    b = s->hs_buf;
    /* C0或S0: 第一个字节必须是 RTMP 协议的版本号"03" */
    if (*b->pos != '\x03') {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "handshake: unexpected RTMP version: %i",
                (ngx_int_t)*b->pos);
        return NGX_ERROR;
    }

    /* 接下来是 C1 或 S1 */

    /* 版本号之后是客户端的 epoch 时间 */
    ++b->pos;
    s->peer_epoch = 0;
    ngx_rtmp_rmemcpy(&s->peer_epoch, b->pos, 4);

    /* 再接下来的四字节是客户端的版本号 */
    p = b->pos + 4;
    ngx_log_debug5(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: peer version=%i.%i.%i.%i epoch=%uD",
            (ngx_int_t)p[3], (ngx_int_t)p[2],
            (ngx_int_t)p[1], (ngx_int_t)p[0],
            (uint32_t)s->peer_epoch);
    if (*(uint32_t *)p == 0) {
        s->hs_old = 1;
        return NGX_OK;
    }

    /* 找到key和digest，进行验证 */
    offs = ngx_rtmp_find_digest(b, peer_key, 772, s->connection->log);
    if (offs == NGX_ERROR) {
        offs = ngx_rtmp_find_digest(b, peer_key, 8, s->connection->log);
    }
    if (offs == NGX_ERROR) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "handshake: digest not found");
        s->hs_old = 1;
        return NGX_OK;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: digest found at pos=%i", offs);
    /* 这里 b->pos 和 b->last 分别指向找到 digest-data 数据区的起始和结尾地址 */
    b->pos += offs;
    b->last = b->pos + NGX_RTMP_HANDSHAKE_KEYLEN;
    /* 下面是计算服务器的 digest */
    s->hs_digest = ngx_palloc(s->connection->pool, NGX_RTMP_HANDSHAKE_KEYLEN);
    if (ngx_rtmp_make_digest(key, b, NULL, s->hs_digest, s->connection->log)
            != NGX_OK)
    {
        return NGX_ERROR;
    }
    return NGX_OK;
}


/*
 * RTMP complex handshake: C2/S2
 * S2: 前 1504 bytes数据随机生成（前 8 bytes需要注意），然后对 1504 bytes 数据进行HMACsha256
 * 得到digest，将digest放到最后的32bytes。
 *
 * 1536 bytes C2/S2 结构  
 * random-data: 1504 bytes  
 * digest-data: 32 bytes
 */
static ngx_int_t
ngx_rtmp_handshake_create_response(ngx_rtmp_session_t *s)
{
    ngx_buf_t          *b;
    u_char             *p;
    ngx_str_t           key;

    b = s->hs_buf;
    b->pos = b->last = b->start + 1;
    ngx_rtmp_fill_random_buffer(b);
    if (s->hs_digest) {
        /* p 指向最后的 32 bytes 的首地址处 */
        p = b->last - NGX_RTMP_HANDSHAKE_KEYLEN;
        key.data = s->hs_digest;
        key.len = NGX_RTMP_HANDSHAKE_KEYLEN;
        /* 将生成的 digest 放置到 s2 数据的最后 32 bytes 中 */
        if (ngx_rtmp_make_digest(&key, b, p, p, s->connection->log) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static void
ngx_rtmp_handshake_done(ngx_rtmp_session_t *s)
{
    /* 释放 handshake 过程使用的缓存 hs_buf，其实，主要是将其
     * 插入到 ngx_rtmp_core_srv_conf_t 的 free_hs 成员所持的
     * 链表表头 */
    ngx_rtmp_free_handshake_buffers(s);

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: done");

    /* 在 events 数组中查找是否有 RTMP 模块设置有 NGX_RTMP_HANDSHAKE_DONE 的回调
     * 方法，有则一一调用，否则不做任何处理 */
    if (ngx_rtmp_fire_event(s, NGX_RTMP_HANDSHAKE_DONE,
                NULL, NULL) != NGX_OK)
    {
        ngx_rtmp_finalize_session(s);
        return;
    }
    // 接收 C2 服务端握手结束，服务器即进入循环处理客户端的请求阶段：ngx_rtmp_cycle
    ngx_rtmp_cycle(s); /* 下面进入 rtmp 业务循环 */
    /*在所有的 RTMP 模块中，仅有 ngx_rtmp_relay_module 模块设置了 NGX_RTMP_HANDSHAKE_DONE 的回调方法：*/
}


static void
ngx_rtmp_handshake_recv(ngx_event_t *rev)
{
    ssize_t                     n;
    ngx_connection_t           *c;
    ngx_rtmp_session_t         *s;
    ngx_buf_t                  *b;

    c = rev->data;
    s = c->data;

    /* 检测该连接是否已经被销毁了 */
    if (c->destroyed) {
        return;
    }

    /* 检测该读事件是否已经超时 */
    if (rev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT,
                "handshake: recv: client timed out");
        c->timedout = 1;
        ngx_rtmp_finalize_session(s);
        return;
    }

    /*
     * 每次调用recv没有接收到客户端的数据时，都会把该事件添加到epoll的
     * 读事件中，同时将该事件也添加到定时器中，并设置该读事件的回调函数为
     * ngx_rtmp_handshake_recv方法，因此，当监听到客户端发送的数据而再次
     * 调用该方法时，需要将该事件从定时器中移除。
     */
    if (rev->timer_set) {
        ngx_del_timer(rev);
    }

    /* b 指向 handshake 的数据缓存首地址 */
    b = s->hs_buf;

    /* 当 handshake 的缓存未满时 */
    while (b->last != b->end) {
        /* 调用 c->recv 指向的回调函数接收数据，实际调用的是
         * ngx_unix_recv 方法 */
        n = c->recv(c, b->last, b->end - b->last);

        if (n == NGX_ERROR || n == 0) {
            ngx_rtmp_finalize_session(s);
            return;
        }

        /* 若返回值为 NGX_AGAIN，则将该读事件再次添加到定时器中，并将
         * 也添加到 epoll 等监控机制中 */
        if (n == NGX_AGAIN) {
            ngx_add_timer(rev, s->timeout);
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                ngx_rtmp_finalize_session(s);
            }
            return;
        }

        /* 若成功接收到数据，则更新 b->last 指针 */
        b->last += n;
    }

    /* 将该读事件从 epoll 等事件监控机制中删除 */
    if (rev->active) {
        ngx_del_event(rev, NGX_READ_EVENT, 0);
    }

    /* 接收到客户端发来的数据后，更新当前 handshake 阶段 */
    ++s->hs_stage;
    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: stage %ui", s->hs_stage);

    switch (s->hs_stage) {
        /* 服务端发送 S0、S1 阶段 */
        case NGX_RTMP_HANDSHAKE_SERVER_SEND_CHALLENGE:
            /* 
             * 此时接收到包应是 client 发来的 RTMP complex handshake 的 C0 和 C1 包.
             * 因此解析 C0+C1，并进行验证. */
            if (ngx_rtmp_handshake_parse_challenge(s,
                    &ngx_rtmp_client_partial_key,
                    &ngx_rtmp_server_full_key) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                        "handshake: error parsing challenge");
                ngx_rtmp_finalize_session(s);
                return;
            }
            if (s->hs_old) {
                ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                        "handshake: old-style challenge");
                s->hs_buf->pos = s->hs_buf->start;
                s->hs_buf->last = s->hs_buf->end;
            } 
            /* 解析完客户端发送来的 C0 和 C1 并进行验证成功后，接着构建 S0+S1，
             * 发送给客户端. */
            else if (ngx_rtmp_handshake_create_challenge(s,
                        ngx_rtmp_server_version,
                        &ngx_rtmp_server_partial_key) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                        "handshake: error creating challenge");
                ngx_rtmp_finalize_session(s);
                return;
            }
            /* 发送 S0+S1，然后该函数接着构建 S2 并发送 */
            ngx_rtmp_handshake_send(c->write);
            break;

        /* 服务端握手结束阶段 */
        case NGX_RTMP_HANDSHAKE_SERVER_DONE:
            /* 这里表示接收到了客户端发来的 handshake 过程的最后一个packet C2 */
            ngx_rtmp_handshake_done(s);
            break;

        /* 作为客户端接受响应阶段 */
        case NGX_RTMP_HANDSHAKE_CLIENT_RECV_RESPONSE:
            if (ngx_rtmp_handshake_parse_challenge(s,
                    &ngx_rtmp_server_partial_key,
                    &ngx_rtmp_client_full_key) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                        "handshake: error parsing challenge");
                ngx_rtmp_finalize_session(s);
                return;
            }
            s->hs_buf->pos = s->hs_buf->last = s->hs_buf->start + 1;
            ngx_rtmp_handshake_recv(c->read);
            break;

        /* 作为客户端发送响应阶段 */
        case NGX_RTMP_HANDSHAKE_CLIENT_SEND_RESPONSE:
            if (ngx_rtmp_handshake_create_response(s) != NGX_OK) {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                        "handshake: response error");
                ngx_rtmp_finalize_session(s);
                return;
            }
            ngx_rtmp_handshake_send(c->write);
            break;
    }
}


static void
ngx_rtmp_handshake_send(ngx_event_t *wev)
{
    ngx_int_t                   n;
    ngx_connection_t           *c;
    ngx_rtmp_session_t         *s;
    ngx_buf_t                  *b;

    c = wev->data;
    s = c->data;

    if (c->destroyed) {
        return;
    }

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT,
                "handshake: send: client timed out");
        c->timedout = 1;
        ngx_rtmp_finalize_session(s);
        return;
    }

    /* 将 wev 写事件从定时器中移除 */
    if (wev->timer_set) {
        ngx_del_timer(wev);
    }

    b = s->hs_buf;

    /* 当 handshake 缓存中有数据时 */
    while(b->pos != b->last) {
        // 缓存区数据不为空 /* 调用 c->send 指向的回调函数(即 ngx_unix_send 方法)发送数据 */
        n = c->send(c, b->pos, b->last - b->pos);

        if (n == NGX_ERROR) {
            ngx_rtmp_finalize_session(s);
            return;
        }

        if (n == NGX_AGAIN || n == 0) {
            ngx_add_timer(c->write, s->timeout);
            if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
                ngx_rtmp_finalize_session(s);
            }
            return;
        }

        b->pos += n;
    }

    /* 若 wev 写事件为活跃的，则将其从 epoll 等事件监控机制中移除 */
    if (wev->active) {
        ngx_del_event(wev, NGX_WRITE_EVENT, 0);
    }

    /* 更新当前 handshake 阶段 */
    ++s->hs_stage;
    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: stage %ui", s->hs_stage);

    switch (s->hs_stage) {
        case NGX_RTMP_HANDSHAKE_SERVER_SEND_RESPONSE:
            if (s->hs_old) {
                ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                        "handshake: old-style response");
                s->hs_buf->pos = s->hs_buf->start + 1;
                s->hs_buf->last = s->hs_buf->end;
            } 
            /* 这里构建 S2 */
            else if (ngx_rtmp_handshake_create_response(s) != NGX_OK) {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                        "handshake: response error");
                ngx_rtmp_finalize_session(s);
                return;
            }
            /* 发送 S2 */
            ngx_rtmp_handshake_send(wev);
            break;

        case NGX_RTMP_HANDSHAKE_SERVER_RECV_RESPONSE:
            s->hs_buf->pos = s->hs_buf->last = s->hs_buf->start + 1;
            ngx_rtmp_handshake_recv(c->read);
            break;

        case NGX_RTMP_HANDSHAKE_CLIENT_RECV_CHALLENGE:
            s->hs_buf->pos = s->hs_buf->last = s->hs_buf->start;
            ngx_rtmp_handshake_recv(c->read);
            break;

        case NGX_RTMP_HANDSHAKE_CLIENT_DONE:
            ngx_rtmp_handshake_done(s);
            break;
    }
}


/*在没有开启代理服务的情况下，初始化完一个 ngx_rtmp_session_t 后，直接进行 rtmp 的 handshake 过程
该函数主要设置读写事件的回调函数，分配 handhshake 过程的数据缓存空间，并初始化当前 handshake 阶段*/
void
ngx_rtmp_handshake(ngx_rtmp_session_t *s)
{
    ngx_connection_t           *c;

    c = s->connection;
    /* 设置当前连接的读写事件回调方法 */
    c->read->handler =  ngx_rtmp_handshake_recv;
    c->write->handler = ngx_rtmp_handshake_send;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: start server handshake");

    /* 分配 handshake 过程的数据缓存 */
    s->hs_buf = ngx_rtmp_alloc_handshake_buffer(s); // 分配内存给记录握手阶段的buf
    /* 初始化当前 handshake 的阶段 */
    // 第一阶段，服务端等待接收客户端发送的 C0 和 C1 阶段。
    s->hs_stage = NGX_RTMP_HANDSHAKE_SERVER_RECV_CHALLENGE;

    /* 开始读取客户端发来的 C0，C1 数据 */
    ngx_rtmp_handshake_recv(c->read);
}


void
ngx_rtmp_client_handshake(ngx_rtmp_session_t *s, unsigned async)
{
    ngx_connection_t           *c;

    c = s->connection;
    /* 设置当前连接读写事件的回调函数 */
    c->read->handler =  ngx_rtmp_handshake_recv;
    c->write->handler = ngx_rtmp_handshake_send;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "handshake: start client handshake");

    /* 为该将要进行的 handshake 过程分配数据缓存，用于存储接收/响应的 hanshake 包 */
    s->hs_buf = ngx_rtmp_alloc_handshake_buffer(s);
    /* 设置当前 handshake 阶段，即为 client send: C0 + C1 */
    s->hs_stage = NGX_RTMP_HANDSHAKE_CLIENT_SEND_CHALLENGE;

    /* 构建 C0 + C1 的 数据包 */
    if (ngx_rtmp_handshake_create_challenge(s,
                ngx_rtmp_client_version,
                &ngx_rtmp_client_partial_key) != NGX_OK)
    {
        ngx_rtmp_finalize_session(s);
        return;
    }

    /* 有前面的调用传入的参数可知，该值为 1，即为异步，因此这里暂时不向上游服务器发送 handshake，
     * 而是将其写事件添加到定时器和 epoll 中，等待下次循环监控到该写事件可写时才发送 C0 + C1 */
    if (async) {
        /* 将该写事件添加到定时器中，超时时间为 s->timeout */
        ngx_add_timer(c->write, s->timeout);
        /* 将该写事件添加到 epoll 等事件监控机制中 */
        if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
            ngx_rtmp_finalize_session(s);
        }
        return;
    }

    ngx_rtmp_handshake_send(c->write);
}

