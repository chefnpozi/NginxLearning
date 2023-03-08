
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_bitop.h"


void
ngx_rtmp_bit_init_reader(ngx_rtmp_bit_reader_t *br, u_char *pos, u_char *last)
{
    ngx_memzero(br, sizeof(ngx_rtmp_bit_reader_t));

    br->pos = pos;
    br->last = last;
}


uint64_t
ngx_rtmp_bit_read(ngx_rtmp_bit_reader_t *br, ngx_uint_t n)
{
    uint64_t    v;
    ngx_uint_t  d;

    v = 0;

    while (n) {

        /* 若已经读取到尾部，则置位错误标志位 */
        if (br->pos >= br->last) {
            br->err = 1;
            return 0;
        }

        /* 控制一次读取的 bit 数不超过 8 bit */
        d = (br->offs + n > 8 ? (ngx_uint_t) (8 - br->offs) : n);

        /* 将读取到的值追加到 v 中 */
        v <<= d;
        v += (*br->pos >> (8 - br->offs - d)) & ((u_char) 0xff >> (8 - d));

        /* 更新 bit reader 的 偏移值 offs */
        br->offs += d;
        n -= d;

        /* 若偏移值为8，则重置该偏移值 */
        if (br->offs == 8) {
            br->pos++;
            br->offs = 0;
        }
    }

    return v;
}


uint64_t
ngx_rtmp_bit_read_golomb(ngx_rtmp_bit_reader_t *br)
{
    ngx_uint_t  n;

    for (n = 0; ngx_rtmp_bit_read(br, 1) == 0 && !br->err; n++);

    return ((uint64_t) 1 << n) + ngx_rtmp_bit_read(br, n) - 1;
}
