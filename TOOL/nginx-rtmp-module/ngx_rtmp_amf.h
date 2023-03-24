
/*
 * Copyright (C) Roman Arutyunyan
 */


#ifndef _NGX_RTMP_AMF_H_INCLUDED_
#define _NGX_RTMP_AMF_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/* basic types */
#define NGX_RTMP_AMF_NUMBER             0x00
#define NGX_RTMP_AMF_BOOLEAN            0x01
#define NGX_RTMP_AMF_STRING             0x02
#define NGX_RTMP_AMF_OBJECT             0x03
#define NGX_RTMP_AMF_NULL               0x05
#define NGX_RTMP_AMF_ARRAY_NULL         0x06
#define NGX_RTMP_AMF_MIXED_ARRAY        0x08
#define NGX_RTMP_AMF_END                0x09
#define NGX_RTMP_AMF_ARRAY              0x0a

/* extended types */
#define NGX_RTMP_AMF_INT8               0x0100
#define NGX_RTMP_AMF_INT16              0x0101
#define NGX_RTMP_AMF_INT32              0x0102
#define NGX_RTMP_AMF_VARIANT_           0x0103

/* r/w flags */
#define NGX_RTMP_AMF_OPTIONAL           0x1000
#define NGX_RTMP_AMF_TYPELESS           0x2000
#define NGX_RTMP_AMF_CONTEXT            0x4000

#define NGX_RTMP_AMF_VARIANT            (NGX_RTMP_AMF_VARIANT_\
                                        |NGX_RTMP_AMF_TYPELESS)

// 接收或发送给客户端的 AMF 数据都是以该结构体的形式组织的。
typedef struct {
    /* 指定要获取的 amf 数据类型 */
    ngx_int_t                           type;
    /* 指定获取的 amf 名称 */
    ngx_str_t                           name;
    /* 将获取到的数据保存到该指针指向的内存中 */
    void                               *data;
    /* data 指向的内存的容量 */
    size_t                              len;
} ngx_rtmp_amf_elt_t;


typedef ngx_chain_t * (*ngx_rtmp_amf_alloc_pt)(void *arg);


typedef struct {
    /* link 指向保存着接收到的数据的 ngx_chain_t 类型的结构体 in 首地址 */
    ngx_chain_t                        *link, *first;
    /* 数据的偏移值 */
    size_t                              offset;
    ngx_rtmp_amf_alloc_pt               alloc;
    void                               *arg;
    ngx_log_t                          *log;
} ngx_rtmp_amf_ctx_t;


/* reading AMF */
ngx_int_t ngx_rtmp_amf_read(ngx_rtmp_amf_ctx_t *ctx,
        ngx_rtmp_amf_elt_t *elts, size_t nelts);

/* writing AMF */
ngx_int_t ngx_rtmp_amf_write(ngx_rtmp_amf_ctx_t *ctx,
        ngx_rtmp_amf_elt_t *elts, size_t nelts);


#endif /* _NGX_RTMP_AMF_H_INCLUDED_ */

