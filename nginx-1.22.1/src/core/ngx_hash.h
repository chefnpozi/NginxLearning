
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HASH_H_INCLUDED_
#define _NGX_HASH_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    void             *value;        /* 指向用户自定义元素数据的指针，如果当前 ngx_hash_elt_t 槽为空，则 value 的值为 0 */
    u_short           len;          // 元素关键字的长度
    u_char            name[1];      // 元素关键字的首地址
} ngx_hash_elt_t;


typedef struct {
    ngx_hash_elt_t  **buckets;      // 指向散列表的首地址，也就是第一个槽的地址
    ngx_uint_t        size;         // 散列表的槽的总数
} ngx_hash_t;


typedef struct {
    ngx_hash_t        hash;
    void             *value;
} ngx_hash_wildcard_t;


typedef struct {
    ngx_str_t         key;          // 元素关键字
    ngx_uint_t        key_hash;     // 由散列函数计算得到的关键码
    void             *value;        // 实际存储的数据
} ngx_hash_key_t;


typedef ngx_uint_t (*ngx_hash_key_pt) (u_char *data, size_t len);


typedef struct {
    ngx_hash_t            hash;
    ngx_hash_wildcard_t  *wc_head;
    ngx_hash_wildcard_t  *wc_tail;
} ngx_hash_combined_t;


typedef struct {
    ngx_hash_t       *hash;         // 指向普通的完全匹配散列表
    ngx_hash_key_pt   key;          // 用于初始化预添加元素的散列方法

    ngx_uint_t        max_size;     // 散列表中槽的最大数目
    ngx_uint_t        bucket_size;  // 散列表中一个槽的空间大小，它限制了每个散列表元素关键字的最大长度（value只是一个指针，所占的空间是固定的）

    char             *name;         // 散列表的名称
    ngx_pool_t       *pool;         // 内存池，它分配散列表中所有的槽，最多分配三个，普通散列表、前置和后置通配符散列表 各一个
    ngx_pool_t       *temp_pool;    // 临时内存池，存在于初始化散列表之前。主要用于分配一些临时的动态数组，带通配符的元素初始化时会用到
} ngx_hash_init_t;


#define NGX_HASH_SMALL            1
#define NGX_HASH_LARGE            2

#define NGX_HASH_LARGE_ASIZE      16384
#define NGX_HASH_LARGE_HSIZE      10007

#define NGX_HASH_WILDCARD_KEY     1
#define NGX_HASH_READONLY_KEY     2


typedef struct {
    /*
        下面的keys_hash, dns_wc_head_hash, dns_wc_tail_hash 都是简易散列表
        hsize指明了散列表中槽的个数，简易散列函数便是对 hsize 求余
    */
    ngx_uint_t        hsize;
    
    ngx_pool_t       *pool;         // 用于分配永久内存，目前该字段没有实际意义
    ngx_pool_t       *temp_pool;    // 临时内存池，分配下面的动态数组需要的内存

    // 该动态数组以 ngx_hash_key_t 为结构体，保存不含有通配符关键字的元素
    ngx_array_t       keys;
    /*  简易散列表，以数组形式保存着 hsize 个元素，每一个元素都是结构为 ngx_array_t 的动态数组（该动态数组存储类型为ngx_str_t，表示一个个的关键字，每个关键字经由散列函数计算的散列码相同），
        用户在添加元素的过程中，会根据关键码将用户的 ngx_str_t 类型的关键字添加到 ngx_array_t 数组中
        这里所有用户的关键字都不带有通配符，表示精确匹配 */
    ngx_array_t      *keys_hash;

    // 该动态数组以 ngx_hash_key_t 为结构体，保存含有前置通配符关键字生成的中间关键字
    ngx_array_t       dns_wc_head;
    /*  该散列表存储的用户元素的关键字带有前置通配符，其余特点同上  */
    ngx_array_t      *dns_wc_head_hash;

    // 该动态数组以 ngx_hash_key_t 为结构体，保存含有后置通配符关键字生成的中间关键字
    ngx_array_t       dns_wc_tail;
    // 该散列表存储的用户元素的关键字带有后置通配符，其余特点同上
    ngx_array_t      *dns_wc_tail_hash;
} ngx_hash_keys_arrays_t;


typedef struct {
    ngx_uint_t        hash;
    ngx_str_t         key;
    ngx_str_t         value;
    u_char           *lowcase_key;
} ngx_table_elt_t;


void *ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len);
void *ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key,
    u_char *name, size_t len);

ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);
ngx_int_t ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);

#define ngx_hash(key, c)   ((ngx_uint_t) key * 31 + c)
ngx_uint_t ngx_hash_key(u_char *data, size_t len);
ngx_uint_t ngx_hash_key_lc(u_char *data, size_t len);
ngx_uint_t ngx_hash_strlow(u_char *dst, u_char *src, size_t n);


ngx_int_t ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type);
ngx_int_t ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key,
    void *value, ngx_uint_t flags);


#endif /* _NGX_HASH_H_INCLUDED_ */
