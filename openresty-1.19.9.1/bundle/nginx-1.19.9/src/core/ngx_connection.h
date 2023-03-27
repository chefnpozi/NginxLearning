
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONNECTION_H_INCLUDED_
#define _NGX_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_listening_s  ngx_listening_t;

/*ngx_cycle_t 对象中有一个动态数组成员叫做 listening，
它的每个数组元素都是 ngx_listening_t 结构体，而每个 ngx_listening_t 结构体又代表着 Nginx 服务器监听的一个端口*/
struct ngx_listening_s {
    // socket 套接字句柄
    ngx_socket_t        fd;

    // 监听 sockaddr 地址
    struct sockaddr    *sockaddr;
    // sockaddr 地址长度
    socklen_t           socklen;    /* size of sockaddr */
    // 存储 IP 地址的字符串 addr_text 最大长度，即它指定了 addr_text 所分配的内存大小
    size_t              addr_text_max_len;
    // 以字符串形式存储IP地址
    ngx_str_t           addr_text;

    // 套接字类型
    int                 type;

    // TCP 实现监听时的 backlog 队列，它表示允许正在通过三次握手建立 TCP 
    // 连接但没有任何进程开始处理的连接最大个数
    int                 backlog;
    // 内核中对于这个套接字的接收缓存区大小
    int                 rcvbuf;
    // 内核中对于这个套接字的发送缓冲区大小
    int                 sndbuf;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    int                 keepidle;
    int                 keepintvl;
    int                 keepcnt;
#endif

    /* handler of accepted connection */
    // 当新的 TCP 连接建立成功后调用的回调处理函数
    ngx_connection_handler_pt   handler;

    // 实际上框架并不使用 servers 指针，它更多的是作为一个保留指针，目前
    // 主要用于HTTP或mail等模块，用于保存当前监听端口对应着的所有主机名
    void               *servers;  /* array of ngx_http_in_addr_t, for example */

    ngx_log_t           log;
    ngx_log_t          *logp;

    // 如果为新的 TCP 连接创建内存池，则内存池的大小为 pool_size
    size_t              pool_size;
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;

    // 前一个ngx_listening_t结构，多个ngx_listening_t结构体之间由previous指针组成单链表
    ngx_listening_t    *previous;
    // 当前监听句柄对应着的ngx_connection_t结构体
    ngx_connection_t   *connection;

    ngx_rbtree_t        rbtree;
    ngx_rbtree_node_t   sentinel;

    ngx_uint_t          worker;

    // 标志位，为1则表示当前监听句柄有效，且执行ngx_init_cycle时不关闭监听端口，
    // 为0时则正常关闭。该标志位框架代码会自动设置
    unsigned            open:1;
    // 标志位，为1表示使用已有的ngx_cycle_t来初始化新的ngx_cycle_t结构体时，不关闭
    // 原先打开的监听端口，这对运行中升级程序很有用，remain为0时，表示正常关闭
    // 曾经打开的监听端口。该标志位框架代码会自动设置
    unsigned            remain:1;
    // 标志位，为1时表示跳过设置当前ngx_listening_t结构体中的套接字，为0时正常
    // 初始化套接字。该标志位框架代码会自动设置
    unsigned            ignore:1;

    // 表示是否已经绑定。
    unsigned            bound:1;       /* already bound */
    // 表示当前监听套接字是否来自前一个进程（如升级Nginx），如果为1，则表示来自前
    // 一个进程。一般会保留之前已经设置好的套接字，不做改变
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    // 标志位，为1时表示当前结构体对应的套接字已经监听
    unsigned            listen:1;
    // 表示套接字是否是非阻塞的
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    // 标志位，为1时表示Nginx会将网络地址转变为字符串形式的地址
    unsigned            addr_ntop:1;
    unsigned            wildcard:1;

#if (NGX_HAVE_INET6)
    unsigned            ipv6only:1;
#endif
    unsigned            reuseport:1;
    unsigned            add_reuseport:1;
    unsigned            keepalive:2;

    unsigned            deferred_accept:1;
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    char               *accept_filter;
#endif
#if (NGX_HAVE_SETFIB)
    int                 setfib;
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
    int                 fastopen;
#endif

};


typedef enum {
    NGX_ERROR_ALERT = 0,
    NGX_ERROR_ERR,
    NGX_ERROR_INFO,
    NGX_ERROR_IGNORE_ECONNRESET,
    NGX_ERROR_IGNORE_EINVAL
} ngx_connection_log_error_e;


typedef enum {
    NGX_TCP_NODELAY_UNSET = 0,
    NGX_TCP_NODELAY_SET,
    NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;


typedef enum {
    NGX_TCP_NOPUSH_UNSET = 0,
    NGX_TCP_NOPUSH_SET,
    NGX_TCP_NOPUSH_DISABLED
} ngx_connection_tcp_nopush_e;


#define NGX_LOWLEVEL_BUFFERED  0x0f
#define NGX_SSL_BUFFERED       0x01
#define NGX_HTTP_V2_BUFFERED   0x02


/*被动连接*/
struct ngx_connection_s {
    /* 连接未使用时，data成员用于充当连接池中空闲连接表中的next指针。当连接被使用时，data的意义
     * 由使用它的Nginx模块而定，如在HTTP框架中，data指向ngx_http_request_t请求 */
    void               *data;
    /* 连接对应的读事件 */
    ngx_event_t        *read;
    /* 连接对应的写事件 */
    ngx_event_t        *write;

    /* 套接字句柄 */
    ngx_socket_t        fd;

    /* 直接接收网络字符流的方法 */
    ngx_recv_pt         recv;
    /* 直接发送网络字符流的方法 */
    ngx_send_pt         send;
    /* 以ngx_chain_t链表为参数来接收网络字符流的方法 */
    ngx_recv_chain_pt   recv_chain;
    /* 以ngx_chain_t链表为参数来发送网络字符流的方法 */
    ngx_send_chain_pt   send_chain;

    /* 这个连接对应的ngx_listening_t监听对象，此连接由listening监听端口的事件建立 */
    ngx_listening_t    *listening;

    /* 这个连接已经发送出去的字节数 */
    off_t               sent;

    ngx_log_t          *log;

    /* 内存池。一般在accept一个新连接时，会创建一个内存池，而在这个连接结束时会销毁内存池。注意，
     * 这里所说的连接是指成功建立的TCP连接，所有的ngx_connection_t结构体都是预分配的。这个内存池
     * 的大小将由上面的listening监听对象中的pool_size成员决定 */
    ngx_pool_t         *pool;

    int                 type;

    /* 连接客户端的sockaddr结构体 */
    struct sockaddr    *sockaddr;
    socklen_t           socklen;
    /* 连接客户端字符串形式的IP地址 */
    ngx_str_t           addr_text;

    ngx_proxy_protocol_t  *proxy_protocol;

#if (NGX_SSL || NGX_COMPAT)
    ngx_ssl_connection_t  *ssl;
#endif

    ngx_udp_connection_t  *udp;

    /* 本机的监听端口对应的sockaddr结构体，也就是listening监听对象中的sockaddr成员 */
    struct sockaddr    *local_sockaddr;
    socklen_t           local_socklen;

    /* 用于接收、缓存客户端发来的字符流，每个事件消费模块可自由决定从连接池中分配多大的空间给
     * buffer这个接收缓存字段。例如，在HTTP模块中，它的大小决定于client_header_buffer_size配置项 */
    ngx_buf_t          *buffer;

    /* 该字段用来将当前连接以双向链表元素的形式添加到ngx_cycle_t核心结构体的
     * reuseable_connections_queue双向链表中，表示可重用的连接 */
    ngx_queue_t         queue;

    /* 连接使用次数。ngx_connection_t结构体每次建立一条来自客户端的连接，或者用于主动向后端服务器
     * 发送连接时（ngx_peer_connection_t也使用它），number都会加1 */
    ngx_atomic_uint_t   number;

    /* 处理的请求次数 */
    ngx_uint_t          requests;

    /*
     * 缓存中的业务类型。任何事件消费模块都可以自定义需要的标志位。这个buffered字段有8位，最多可以同时表示
     * 8个不同的业务。第三方模块在自定义buffered标志位时注意不要与可能使用的模块定义的标志位冲突。
     * openssl模块定义了一个标志位：
     * #define NGX_SSL_BUFFERED                   0x01
     * HTTP官方模块定义了以下标志位：
     * #define NGX_HTTP_LOWLEVEL_BUFFERED         0xf0
     * #define NGX_HTTP_WRITE_BUFFERED            0x10
     * #define NGX_HTTP_GZIP_BUFFERED             0x20
     * #define NGX_HTTP_SSI_BUFFERED              0x01
     * #define NGX_HTTP_SUB_BUFFERED              0x02
     * #define NGX_HTTP_COPY_BUFFERED             0x04
     * 对于HTTP模块而言，buffered的低4位要慎用，在实际发送响应的ngx_http_write_filter_module
     * 过滤模块中，低4位标志位为1则意味着Nginx会一直认为有HTTP模块还需要处理这个请求，必须等待
     * HTTP模块将低4位全置为0才会正常结束请求。检查低4位的宏如下：
     * #define NGX_HTTP_LOWLEVEL_BUFFERED         0xf0
     */
    unsigned            buffered:8;

    /* 本连接记录日志时的级别，它占用3位，取值范围是0~7，但实际上目前只定义了5个值，由
     * ngx_connection_log_error_e枚举表示，如下：
     * typedef enum {
     * NGX_ERROR_ALERT = 0,
     * NGX_ERROR_ERR,
     * NGX_ERROR_INFO,
     * NGX_ERROR_IGNORE_ECONNRESET,
     * NGX_ERROR_IGNORE_EINVAL
     * } ngx_connection_log_error_e;
     */
    unsigned            log_error:3;     /* ngx_connection_log_error_e */

    /* 标志位，为1时表示连接已经超时 */
    unsigned            timedout:1;
    /* 标志位，为1时表示连接处理过程中出现错误 */
    unsigned            error:1;
    /* 标志位，为1时表示连接已经销毁。这里的连接指的是TCP连接，而不是ngx_connection_t结构体。
     * 当destroyed为1时，ngx_connection_t结构体仍然存在，但其对应的套接字、内存池等已经不可用 */
    unsigned            destroyed:1;

    /* 标志位，为1时表示连接处于空闲状态，如keepalive请求中两次请求之间的状态 */
    unsigned            idle:1;
    /* 标志位，为1时表示连接可重用，它与上面的queue字段是对应使用的 */
    unsigned            reusable:1;
    /* 标志位，为1时表示连接关闭 */
    unsigned            close:1;
    unsigned            shared:1;

    /* 标志位，为1时表示正在将文件中的数据发往连接的另一端 */
    unsigned            sendfile:1;
    /* 标志位，如果为1，则表示只有在链接套接字对应的发送缓冲区必须满足最低设置的大小阈值时，事件
     * 驱动模块才会分发该事件。这与ngx_handle_write_event方法中的lowat参数是对应的 */
    unsigned            sndlowat:1;
    /* 标志位，表示如何使用TCP的nodelay特性。它的取值范围是下面这个枚举类型
     * ngx_connection_tcp_nodelay_e：
     * typedef enum {
     * NGX_TCP_NODELAY_UNSET = 0,
     * NGX_TCP_NODELAY_SET,
     * NGX_TCP_NODELAY_DISABLED
     * } ngx_connection_tcp_nodelay_e;
     */
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
    /* 标志位，表示如何使用TCP的nopush特性。它的取值范围是下面的这个枚举类型
     * ngx_connection_tcp_nopush_e：
     * typedef enum {
     * NGX_TCP_NOPUSH_UNSET = 0,
     * NGX_TCP_NOPUSH_SET,
     * NGX_TCP_NOPUSH_DISABLED
     * } ngx_connection_tcp_nopush_e;
     */
    unsigned            tcp_nopush:2;    /* ngx_connection_tcp_nopush_e */

    unsigned            need_last_buf:1;

#if (NGX_HAVE_AIO_SENDFILE || NGX_COMPAT)
    unsigned            busy_count:2;
#endif

#if (NGX_THREADS || NGX_COMPAT)
    ngx_thread_task_t  *sendfile_task;
#endif
};


#define ngx_set_connection_log(c, l)                                         \
                                                                             \
    c->log->file = l->file;                                                  \
    c->log->next = l->next;                                                  \
    c->log->writer = l->writer;                                              \
    c->log->wdata = l->wdata;                                                \
    if (!(c->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {                   \
        c->log->log_level = l->log_level;                                    \
    }


ngx_listening_t *ngx_create_listening(ngx_conf_t *cf, struct sockaddr *sockaddr,
    socklen_t socklen);
ngx_int_t ngx_clone_listening(ngx_cycle_t *cycle, ngx_listening_t *ls);
ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle);
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle);
void ngx_configure_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_connection(ngx_connection_t *c);
void ngx_close_idle_connections(ngx_cycle_t *cycle);
ngx_int_t ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s,
    ngx_uint_t port);
ngx_int_t ngx_tcp_nodelay(ngx_connection_t *c);
ngx_int_t ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text);

ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log);
void ngx_free_connection(ngx_connection_t *c);

void ngx_reusable_connection(ngx_connection_t *c, ngx_uint_t reusable);

#endif /* _NGX_CONNECTION_H_INCLUDED_ */
