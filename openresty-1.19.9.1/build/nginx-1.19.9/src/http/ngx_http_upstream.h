
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_UPSTREAM_H_INCLUDED_
#define _NGX_HTTP_UPSTREAM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_event_pipe.h>
#include <ngx_http.h>


#define NGX_HTTP_UPSTREAM_FT_ERROR           0x00000002
#define NGX_HTTP_UPSTREAM_FT_TIMEOUT         0x00000004
#define NGX_HTTP_UPSTREAM_FT_INVALID_HEADER  0x00000008
#define NGX_HTTP_UPSTREAM_FT_HTTP_500        0x00000010
#define NGX_HTTP_UPSTREAM_FT_HTTP_502        0x00000020
#define NGX_HTTP_UPSTREAM_FT_HTTP_503        0x00000040
#define NGX_HTTP_UPSTREAM_FT_HTTP_504        0x00000080
#define NGX_HTTP_UPSTREAM_FT_HTTP_403        0x00000100
#define NGX_HTTP_UPSTREAM_FT_HTTP_404        0x00000200
#define NGX_HTTP_UPSTREAM_FT_HTTP_429        0x00000400
#define NGX_HTTP_UPSTREAM_FT_UPDATING        0x00000800
#define NGX_HTTP_UPSTREAM_FT_BUSY_LOCK       0x00001000
#define NGX_HTTP_UPSTREAM_FT_MAX_WAITING     0x00002000
#define NGX_HTTP_UPSTREAM_FT_NON_IDEMPOTENT  0x00004000
#define NGX_HTTP_UPSTREAM_FT_NOLIVE          0x40000000
#define NGX_HTTP_UPSTREAM_FT_OFF             0x80000000

#define NGX_HTTP_UPSTREAM_FT_STATUS          (NGX_HTTP_UPSTREAM_FT_HTTP_500  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_502  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_503  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_504  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_403  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_404  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_429)

#define NGX_HTTP_UPSTREAM_INVALID_HEADER     40


#define NGX_HTTP_UPSTREAM_IGN_XA_REDIRECT    0x00000002
#define NGX_HTTP_UPSTREAM_IGN_XA_EXPIRES     0x00000004
#define NGX_HTTP_UPSTREAM_IGN_EXPIRES        0x00000008
#define NGX_HTTP_UPSTREAM_IGN_CACHE_CONTROL  0x00000010
#define NGX_HTTP_UPSTREAM_IGN_SET_COOKIE     0x00000020
#define NGX_HTTP_UPSTREAM_IGN_XA_LIMIT_RATE  0x00000040
#define NGX_HTTP_UPSTREAM_IGN_XA_BUFFERING   0x00000080
#define NGX_HTTP_UPSTREAM_IGN_XA_CHARSET     0x00000100
#define NGX_HTTP_UPSTREAM_IGN_VARY           0x00000200


typedef struct {
    ngx_uint_t                       status;
    ngx_msec_t                       response_time;
    ngx_msec_t                       connect_time;
    ngx_msec_t                       header_time;
    ngx_msec_t                       queue_time;
    off_t                            response_length;
    off_t                            bytes_received;
    off_t                            bytes_sent;

    ngx_str_t                       *peer;
} ngx_http_upstream_state_t;


typedef struct {
    ngx_hash_t                       headers_in_hash;
    ngx_array_t                      upstreams;
                                             /* ngx_http_upstream_srv_conf_t */
} ngx_http_upstream_main_conf_t;

typedef struct ngx_http_upstream_srv_conf_s  ngx_http_upstream_srv_conf_t;

typedef ngx_int_t (*ngx_http_upstream_init_pt)(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
typedef ngx_int_t (*ngx_http_upstream_init_peer_pt)(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);


typedef struct {
    ngx_http_upstream_init_pt        init_upstream;
    ngx_http_upstream_init_peer_pt   init;
    void                            *data;
} ngx_http_upstream_peer_t;


typedef struct {
    ngx_str_t                        name;
    ngx_addr_t                      *addrs;
    ngx_uint_t                       naddrs;
    ngx_uint_t                       weight;
    ngx_uint_t                       max_conns;
    ngx_uint_t                       max_fails;
    time_t                           fail_timeout;
    ngx_msec_t                       slow_start;
    ngx_uint_t                       down;

    unsigned                         backup:1;

    NGX_COMPAT_BEGIN(6)
    NGX_COMPAT_END
} ngx_http_upstream_server_t;


#define NGX_HTTP_UPSTREAM_CREATE        0x0001
#define NGX_HTTP_UPSTREAM_WEIGHT        0x0002
#define NGX_HTTP_UPSTREAM_MAX_FAILS     0x0004
#define NGX_HTTP_UPSTREAM_FAIL_TIMEOUT  0x0008
#define NGX_HTTP_UPSTREAM_DOWN          0x0010
#define NGX_HTTP_UPSTREAM_BACKUP        0x0020
#define NGX_HTTP_UPSTREAM_MAX_CONNS     0x0100


struct ngx_http_upstream_srv_conf_s {
    ngx_http_upstream_peer_t         peer;
    void                           **srv_conf;

    ngx_array_t                     *servers;  /* ngx_http_upstream_server_t */

    ngx_uint_t                       flags;
    ngx_str_t                        host;
    u_char                          *file_name;
    ngx_uint_t                       line;
    in_port_t                        port;
    ngx_uint_t                       no_port;  /* unsigned no_port:1 */

#if (NGX_HTTP_UPSTREAM_ZONE)
    ngx_shm_zone_t                  *shm_zone;
#endif
};


typedef struct {
    ngx_addr_t                      *addr;
    ngx_http_complex_value_t        *value;
#if (NGX_HAVE_TRANSPARENT_PROXY)
    ngx_uint_t                       transparent; /* unsigned  transparent:1; */
#endif
} ngx_http_upstream_local_t;


typedef struct {
    ngx_http_upstream_srv_conf_t    *upstream;

    ngx_msec_t                       connect_timeout;
    ngx_msec_t                       send_timeout;
    ngx_msec_t                       read_timeout;
    ngx_msec_t                       next_upstream_timeout;

    size_t                           send_lowat;
    size_t                           buffer_size;
    size_t                           limit_rate;

    size_t                           busy_buffers_size;
    size_t                           max_temp_file_size;
    size_t                           temp_file_write_size;

    size_t                           busy_buffers_size_conf;
    size_t                           max_temp_file_size_conf;
    size_t                           temp_file_write_size_conf;

    ngx_bufs_t                       bufs;

    ngx_uint_t                       ignore_headers;
    ngx_uint_t                       next_upstream;
    ngx_uint_t                       store_access;
    ngx_uint_t                       next_upstream_tries;
    ngx_flag_t                       buffering;
    ngx_flag_t                       request_buffering;
    ngx_flag_t                       pass_request_headers;
    ngx_flag_t                       pass_request_body;

    ngx_flag_t                       ignore_client_abort;
    ngx_flag_t                       intercept_errors;
    ngx_flag_t                       cyclic_temp_file;
    ngx_flag_t                       force_ranges;

    ngx_path_t                      *temp_path;

    ngx_hash_t                       hide_headers_hash;
    ngx_array_t                     *hide_headers;
    ngx_array_t                     *pass_headers;

    ngx_http_upstream_local_t       *local;
    ngx_flag_t                       socket_keepalive;

#if (NGX_HTTP_CACHE)
    ngx_shm_zone_t                  *cache_zone;
    ngx_http_complex_value_t        *cache_value;

    ngx_uint_t                       cache_min_uses;
    ngx_uint_t                       cache_use_stale;
    ngx_uint_t                       cache_methods;

    off_t                            cache_max_range_offset;

    ngx_flag_t                       cache_lock;
    ngx_msec_t                       cache_lock_timeout;
    ngx_msec_t                       cache_lock_age;

    ngx_flag_t                       cache_revalidate;
    ngx_flag_t                       cache_convert_head;
    ngx_flag_t                       cache_background_update;

    ngx_array_t                     *cache_valid;
    ngx_array_t                     *cache_bypass;
    ngx_array_t                     *cache_purge;
    ngx_array_t                     *no_cache;
#endif

    ngx_array_t                     *store_lengths;
    ngx_array_t                     *store_values;

#if (NGX_HTTP_CACHE)
    signed                           cache:2;
#endif
    signed                           store:2;
    unsigned                         intercept_404:1;
    unsigned                         change_buffering:1;
    unsigned                         pass_trailers:1;
    unsigned                         preserve_output:1;

#if (NGX_HTTP_SSL || NGX_COMPAT)
    ngx_ssl_t                       *ssl;
    ngx_flag_t                       ssl_session_reuse;

    ngx_http_complex_value_t        *ssl_name;
    ngx_flag_t                       ssl_server_name;
    ngx_flag_t                       ssl_verify;
#endif

    ngx_str_t                        module;

    NGX_COMPAT_BEGIN(2)
    NGX_COMPAT_END
} ngx_http_upstream_conf_t;


typedef struct {
    ngx_str_t                        name;
    ngx_http_header_handler_pt       handler;
    ngx_uint_t                       offset;
    ngx_http_header_handler_pt       copy_handler;
    ngx_uint_t                       conf;
    ngx_uint_t                       redirect;  /* unsigned   redirect:1; */
} ngx_http_upstream_header_t;


typedef struct {
    ngx_list_t                       headers;
    ngx_list_t                       trailers;

    ngx_uint_t                       status_n;
    ngx_str_t                        status_line;

    ngx_table_elt_t                 *status;
    ngx_table_elt_t                 *date;
    ngx_table_elt_t                 *server;
    ngx_table_elt_t                 *connection;

    ngx_table_elt_t                 *expires;
    ngx_table_elt_t                 *etag;
    ngx_table_elt_t                 *x_accel_expires;
    ngx_table_elt_t                 *x_accel_redirect;
    ngx_table_elt_t                 *x_accel_limit_rate;

    ngx_table_elt_t                 *content_type;
    ngx_table_elt_t                 *content_length;

    ngx_table_elt_t                 *last_modified;
    ngx_table_elt_t                 *location;
    ngx_table_elt_t                 *accept_ranges;
    ngx_table_elt_t                 *www_authenticate;
    ngx_table_elt_t                 *transfer_encoding;
    ngx_table_elt_t                 *vary;

#if (NGX_HTTP_GZIP)
    ngx_table_elt_t                 *content_encoding;
#endif

    ngx_array_t                      cache_control;
    ngx_array_t                      cookies;

    off_t                            content_length_n;
    time_t                           last_modified_time;

    unsigned                         connection_close:1;
    unsigned                         chunked:1;
} ngx_http_upstream_headers_in_t;


typedef struct {
    ngx_str_t                        host;
    in_port_t                        port;
    ngx_uint_t                       no_port; /* unsigned no_port:1 */

    ngx_uint_t                       naddrs;
    ngx_resolver_addr_t             *addrs;

    struct sockaddr                 *sockaddr;
    socklen_t                        socklen;
    ngx_str_t                        name;

    ngx_resolver_ctx_t              *ctx;
} ngx_http_upstream_resolved_t;


typedef void (*ngx_http_upstream_handler_pt)(ngx_http_request_t *r,
    ngx_http_upstream_t *u);

struct ngx_http_upstream_s {
    // 处理读事件的回调方法，每一个阶段都有不同的 read_event_handler
    ngx_http_upstream_handler_pt read_event_handler;
    // 处理写事件的回调方法，每一个阶段都有不同的 write_event_handler
    ngx_http_upstream_handler_pt write_event_handler;

    /*表示主动向上游服务器发起的连接。关于ngx_peer_connection_t结构体，可参见9.3.2节*/
    ngx_peer_connection_t peer;

    /* 当向下游客户端转发响应时（ngx_http_request_t结构体中的subrequest_in_memory标志位为0），
       如果打开了缓存且认为上游网速更快（conf配置中的buffering标志位为1），这时会使用pipe成员来转发响应。
       在使用这种方式转发响应时，必须由HTTP模块在使用upstream机制前构造pipe结构体，否则会出现严重的coredump错误。详见12.8.1节 */
    ngx_event_pipe_t *pipe;

    /* request_bufs决定发送什么样的请求给上游服务器，在实现create_request方法时需要设置它。
       request_bufs以链表的方式把ngx_buf_t缓冲区链接起来，它表示所有需要发送到上游服务器的请求内容。
       所以，HTTP模块实现的create_request回调方法就在于构造request_bufs链表*/
    ngx_chain_t *request_bufs;

    // 定义了向下游发送响应的方式
    ngx_output_chain_ctx_t output;
    ngx_chain_writer_ctx_t writer;

    

    // upstream访问时的所有限制性参数，使用upstream机制时的各种配置，详见12.1.3节
    ngx_http_upstream_conf_t *conf;
    ngx_http_upstream_srv_conf_t *upstream;
#if (NGX_HTTP_CACHE)
    ngx_array_t *caches;
#endif

#define HAVE_NGX_UPSTREAM_TIMEOUT_FIELDS 1
    ngx_msec_t connect_timeout;
    ngx_msec_t send_timeout;
    ngx_msec_t read_timeout;

    /* HTTP模块在实现process_header方法时，如果希望upstream直接转发响应，就需要把解析出的响应头部适配为HTTP的响应头部，
       同时需要把包头中的信息设置到headers_in结构体中，这样，在图12-5的第8步中，
       会把headers_in中设置的头部添加到要发送到下游客户端的响应头部headers_out中 */
    ngx_http_upstream_headers_in_t headers_in;

    // 用于解析主机域名，本章不作介绍，通过resolved可以直接指定上游服务器地址，在5.1.3节会详细讨论它
    ngx_http_upstream_resolved_t *resolved;

    ngx_buf_t from_client;

    /* 接收上游服务器响应包头的缓冲区，在不需要把响应直接转发给客户端，或者buffering标志位为0的情况下转发包体时，接收包体的缓冲区仍然使用buffer。
       注意，如果没有自定义input_filter方法处理包体，将会使用buffer存储全部的包体，这时buffer必须足够大！它的大小由ngx_http_upstream_conf_t配置结构体中的buffer_size成员决定。buffer成员存储接收自上游服务器发来的响应内容，由于它会被复用，所以具有下列多种意义：
        a)在使用process_header方法解析上游响应的包头时，buffer中将会保存完整的响应包头；
        b)当下面的buffering成员为1，而且此时upstream是向下游转发上游的包体时，buffer没有意义；
        c)当buffering标志位为0时，buffer缓冲区会被用于反复地接收上游的包体，进而向下游转发；
        d)当upstream并不用于转发上游包体时，buffer会被用于反复接收上游的包体，HTTP模块实现的input_filter方法需要关注它 */
    ngx_buf_t buffer;
    // 表示来自上游服务器的响应包体的长度
    off_t length;

    /* out_bufs在两种场景下有不同的意义：①当不需要转发包体，且使用默认的input_filter方法（也就是ngx_http_upstream_non_buffered_filter方法）处理包体时，
       out_bufs将会指向响应包体，事实上，out_bufs链表中会产生多个ngx_buf_t缓冲区，每个缓冲区都指向buffer缓存中的一部分，而这里的一部分就是每次调用recv方法接收到的一段TCP流。
       ②当需要转发响应包体到下游时（buffering标志位为0，即以下游网速优先，参见12.7节），这个链表指向上一次向下游转发响应到现在这段时间内接收自上游的缓存响应*/
    ngx_chain_t *out_bufs;

    /* 当需要转发响应包体到下游时（buffering标志位为0，即以下游网速优先，参见12.7节），它表示上一次向下游转发响应时没有发送完的内容 */
    ngx_chain_t *busy_bufs;
    /* 这个链表将用于回收out_bufs中已经发送给下游的ngx_buf_t结构体，这同样应用在buffering标志位为0即以下游网速优先的场景 */
    ngx_chain_t *free_bufs;

    /* 处理包体前的初始化方法，其中data参数用于传递用户数据结构，它实际上就是下面的input_filter_ctx指针 */
    ngx_int_t (*input_filter_init)(void *data);
    /* 处理包体的方法，其中data参数用于传递用户数据结构，它实际上就是下面的input_filter_ctx指针，而bytes表示本次接收到的包体长度。返回NGX_ERROR时表示处理包体错误，请求需要结束，否则都将继续upstream流程 */
    ngx_int_t (*input_filter)(void *data, ssize_t bytes);
    /* 用于传递HTTP模块自定义的数据结构，在input_filter_init和input_filter方法被回调时会作为参数传递过去 */
    void *input_filter_ctx;

#if (NGX_HTTP_CACHE)
    ngx_int_t (*create_key)(ngx_http_request_t *r);
#endif
    // HTTP模块实现的create_request方法用于构造发往上游服务器的请求
    ngx_int_t (*create_request)(ngx_http_request_t *r);
    /* 与上游服务器的通信失败后，如果按照重试规则还需要再次向上游服务器发起连接，则会调用reinit_request方法 */
    ngx_int_t (*reinit_request)(ngx_http_request_t *r);

    /* 收到上游服务器的响应后就会回调process_header方法。解析上游服务器返回响应的包头，返回NGX_AGAIN表示包头还没有接收完整，此时，对于本次upstream请求来说，再次接收到上游服务器发来的TCP流时，还会调用process_header方法处理，
       直到process_header函数返回非NGX_AGAIN值这一阶段才会停止。返回NGX_HTTP_UPSTREAM_INVALID_HEADER表示包头不合法，返回NGX_ERROR表示出现错误，返回NGX_OK表示解析到完整的包头 */
    ngx_int_t (*process_header)(ngx_http_request_t *r);
    /* 当前版本下abort_request回调方法没有任意意义，在upstream的所有流程中都不会调用 */
    void (*abort_request)(ngx_http_request_t *r);  
    // 请求结束时会调用，参见 12.9.1节
    void (*finalize_request)(ngx_http_request_t *r,
                             ngx_int_t rc);
    /* 在上游返回的响应出现Location或者Refresh头部表示重定向时，会通过ngx_http_upstream_process_headers方法（参见图12-5中的第8步）调用到可由HTTP模块实现的rewrite_redirect方法 */
    ngx_int_t (*rewrite_redirect)(ngx_http_request_t *r,
                                  ngx_table_elt_t *h, size_t prefix);  
    ngx_int_t (*rewrite_cookie)(ngx_http_request_t *r,
                                ngx_table_elt_t *h);

    ngx_msec_t start_time;
    // 用于表示上游响应的错误码、包体长度等信息
    ngx_http_upstream_state_t *state;

    // 不使用文件缓存，没有意义
    ngx_str_t method;
    // schema和uri成员仅在记录日志时会用到，除此以外没有意义
    ngx_str_t schema;
    ngx_str_t uri;

#if (NGX_HTTP_SSL || NGX_COMPAT)
    ngx_str_t ssl_name;
#endif
    /* 目前它仅用于表示是否需要清理资源，相当于一个标志位，实际不会调用到它所指向的方法*/
    ngx_http_cleanup_pt *cleanup;
    // 是否指定文件缓存路径的标志位，本章不讨论文件缓存，略过
    unsigned store : 1;
    // 是否启用文件缓存，本章仅讨论cacheable标志位为0的场景
    unsigned cacheable : 1;
    // 暂无意义
    unsigned accel : 1;
    // 基于SSL协议访问上游服务器
    unsigned ssl : 1; 
#if (NGX_HTTP_CACHE)
    unsigned cache_status : 3;
#endif
    /* 在向客户端转发上游服务器的包体时才有用。是否开启更大的内存及临时磁盘文件用于缓存来不及发送到下游的响应包体，
       当buffering为1时，表示使用多个缓冲区以及磁盘文件来转发上游的响应包体。当Nginx与上游间的网速远大于Nginx与下游客户端间的网速时，
       让Nginx开辟更多的内存甚至使用磁盘文件来缓存上游的响应包体，这是有意义的，它可以减轻上游服务器的并发压力。
       当buffering为0时，表示只使用上面的这一个buffer缓冲区来向下游转发响应包体 */
    unsigned buffering : 1;
    unsigned keepalive : 1;
    unsigned upgrade : 1;
    unsigned error : 1;

    /* request_sent表示是否已经向上游服务器发送了请求，当request_sent为1时，表示upstream机制已经向上游服务器发送了全部或者部分的请求。
       事实上，这个标志位更多的是为了使用ngx_output_chain方法发送请求，因为该方法发送请求时会自动把未发送完的request_bufs链表记录下来，
       为了防止反复发送重复请求，必须有request_sent标志位记录是否调用过ngx_output_chain方法 */
    unsigned request_sent : 1;
    unsigned request_body_sent : 1;
    unsigned request_body_blocked : 1;
    /* 将上游服务器的响应划分为包头和包尾，如果把响应直接转发给客户端，header_sent标志位表示包头是否发送，
       header_sent为1时表示已经把包头转发给客户端了。如果不转发响应到客户端，则header_sent没有意义 */
    unsigned header_sent : 1;
};

typedef struct {
    ngx_uint_t                      status;
    ngx_uint_t                      mask;
} ngx_http_upstream_next_t;


typedef struct {
    ngx_str_t   key;
    ngx_str_t   value;
    ngx_uint_t  skip_empty;
} ngx_http_upstream_param_t;


ngx_int_t ngx_http_upstream_create(ngx_http_request_t *r);
void ngx_http_upstream_init(ngx_http_request_t *r);
ngx_int_t ngx_http_upstream_non_buffered_filter_init(void *data);
ngx_int_t ngx_http_upstream_non_buffered_filter(void *data, ssize_t bytes);
ngx_http_upstream_srv_conf_t *ngx_http_upstream_add(ngx_conf_t *cf,
    ngx_url_t *u, ngx_uint_t flags);
char *ngx_http_upstream_bind_set_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
char *ngx_http_upstream_param_set_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
ngx_int_t ngx_http_upstream_hide_headers_hash(ngx_conf_t *cf,
    ngx_http_upstream_conf_t *conf, ngx_http_upstream_conf_t *prev,
    ngx_str_t *default_hide_headers, ngx_hash_init_t *hash);


#define ngx_http_conf_upstream_srv_conf(uscf, module)                         \
    uscf->srv_conf[module.ctx_index]


extern ngx_module_t        ngx_http_upstream_module;
extern ngx_conf_bitmask_t  ngx_http_upstream_cache_method_mask[];
extern ngx_conf_bitmask_t  ngx_http_upstream_ignore_headers_masks[];


#ifndef HAVE_BALANCER_STATUS_CODE_PATCH
#define HAVE_BALANCER_STATUS_CODE_PATCH
#endif


#endif /* _NGX_HTTP_UPSTREAM_H_INCLUDED_ */
