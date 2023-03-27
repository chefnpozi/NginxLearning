
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (NGX_TEST_BUILD_EPOLL)

/* epoll declarations */

#define EPOLLIN        0x001
#define EPOLLPRI       0x002
#define EPOLLOUT       0x004
#define EPOLLERR       0x008
#define EPOLLHUP       0x010
#define EPOLLRDNORM    0x040
#define EPOLLRDBAND    0x080
#define EPOLLWRNORM    0x100
#define EPOLLWRBAND    0x200
#define EPOLLMSG       0x400

#define EPOLLRDHUP     0x2000

#define EPOLLEXCLUSIVE 0x10000000
#define EPOLLONESHOT   0x40000000
#define EPOLLET        0x80000000

#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_DEL  2
#define EPOLL_CTL_MOD  3

typedef union epoll_data {
    void         *ptr;
    int           fd;
    uint32_t      u32;
    uint64_t      u64;
} epoll_data_t;

/*events：的取值有
    EPOLLIN: 表示对应的连接上有数据可读（TCP 连接的远端主动关闭连接，也相当于可读事件，因为需要处理发送来的 FIN）
    EPOLLOUT: 表示对应的连接上可以写入数据发送（主动向上游服务器发起非阻塞的 TCP 连接，连接建立成功的事件相当于可写事件）
    EPOLLRDHUP：表示 TCP 连接的远端关闭或半关闭连接
    EPOLLLPRI：表示对应的连接上有紧急数据需要读
    EPOLLERR: 表示对应的连接发生错误
    EPOLLHUP: 表示对应的连接被挂起
    EPOLLLET: 表示将触发方式设置为边缘触发（ET），系统默认为水平触发(LT)
    EPOLLONESHOT: 表示对这个事件只处理一次，下次需要处理时需重新加入 epoll
data：是一个 epoll_data 联合*/
struct epoll_event {
    uint32_t      events;
    epoll_data_t  data;
};


/*系统调用 epoll_create() 创建一个 epoll 的句柄，之后 epoll 的使用都将依靠这个句柄来标识。参数 size 是告知 epoll 所
要处理的大致事件数目。不再使用 epoll 时，必须调用 close 关闭这个句柄。*/
int epoll_create(int size);

int epoll_create(int size)
{
    return -1;
}


/*epoll_ctl 向 epoll 对象中添加、修改或者删除感兴趣的事件，返回 0 表示成功，否则返回 -1，此时需要根据 errno 错误码
判断错误类型。

epfd：是 epoll_create() 返回的句柄；
op：表示动作，可取的值有：
    EPOLL_CTL_ADD: 添加新的事件到 epoll 中
    EPOLL_CTL_MOD: 修改 epoll 中的事件
    EPOLL_CTL_DEL：删除 epoll 中的事件
fd: 需要监听的描述符；
event: 是 epoll_event 结构体类型，用于告诉内核需要监听什么事件。*/
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return -1;
}


/*收集在 epoll 监控的事件中已经发生的事件，如果 epoll 中没有任何一个事件发生，则最多等待 timeout 毫秒后返回。
epoll_wait 的返回值表示当前发生的事件个数，如果返回 0，则表示本次调用中没有事件发生，如果返回 -1，则表示发生错
误，需要检查 errno 判断错误类型。

epfd：是 epoll_create() 返回的句柄；
events：是分配好的 epoll_event 结构体数组，epoll 将会把发生的事件复制到 events 数组中（events 不可以是空指针，
内核只负责把数据复制到这个 events 数组中，不会去帮助我们在用户态中分配内存）
maxevents：表示本次可以返回的最大事件数目，通常 maxevents 参数与预分配的 events 数组的大小是相等的；
timeout：表示在没有检测到事件发生时最多等待的时间（单位为毫秒），如果 timeout 为 0，则表示 epoll_wait 在
rdllist 链表为空时，立刻返回，不会等待。*/
int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout);

int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout)
{
    return -1;
}

#if (NGX_HAVE_EVENTFD)
#define SYS_eventfd       323
#endif

#if (NGX_HAVE_FILE_AIO)

#define SYS_io_setup      245
#define SYS_io_destroy    246
#define SYS_io_getevents  247

typedef u_int  aio_context_t;

struct io_event {
    uint64_t  data;  /* the data field from the iocb */
    uint64_t  obj;   /* what iocb this event came from */
    int64_t   res;   /* result code for this event */
    int64_t   res2;  /* secondary result */
};


#endif
#endif /* NGX_TEST_BUILD_EPOLL */


typedef struct {
    ngx_uint_t  events;
    ngx_uint_t  aio_requests;
} ngx_epoll_conf_t;


static ngx_int_t ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer);
#if (NGX_HAVE_EVENTFD)
static ngx_int_t ngx_epoll_notify_init(ngx_log_t *log);
static void ngx_epoll_notify_handler(ngx_event_t *ev);
#endif
#if (NGX_HAVE_EPOLLRDHUP)
static void ngx_epoll_test_rdhup(ngx_cycle_t *cycle);
#endif
static void ngx_epoll_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_add_connection(ngx_connection_t *c);
static ngx_int_t ngx_epoll_del_connection(ngx_connection_t *c,
    ngx_uint_t flags);
#if (NGX_HAVE_EVENTFD)
static ngx_int_t ngx_epoll_notify(ngx_event_handler_pt handler);
#endif
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
    ngx_uint_t flags);

#if (NGX_HAVE_FILE_AIO)
static void ngx_epoll_eventfd_handler(ngx_event_t *ev);
#endif

static void *ngx_epoll_create_conf(ngx_cycle_t *cycle);
static char *ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf);

static int                  ep = -1;
static struct epoll_event  *event_list;
static ngx_uint_t           nevents;

#if (NGX_HAVE_EVENTFD)
static int                  notify_fd = -1;
static ngx_event_t          notify_event;
static ngx_connection_t     notify_conn;
#endif

#if (NGX_HAVE_FILE_AIO)

int                         ngx_eventfd = -1;
aio_context_t               ngx_aio_ctx = 0;

static ngx_event_t          ngx_eventfd_event;
static ngx_connection_t     ngx_eventfd_conn;

#endif

#if (NGX_HAVE_EPOLLRDHUP)
ngx_uint_t                  ngx_use_epoll_rdhup;
#endif

static ngx_str_t      epoll_name = ngx_string("epoll");

static ngx_command_t  ngx_epoll_commands[] = {

    /* 在调用 epoll_wait 时，将由第 2 和第 3 个参数告诉 Linux 内核一次最多可返回多少个事件。这个配置项
     * 表示调用一次 epoll_wait 时最多可以返回的事件数，当然，它也会预分配那么多 epoll_event 结构体用于
     * 存储事件 */
    { ngx_string("epoll_events"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_epoll_conf_t, events),
      NULL },

    /* 指明在开启异步 I/O 且使用 io_setup 系统调用初始化异步 I/O 上下文环境时，初始分配的异步 I/O 
     * 事件个数 */
    { ngx_string("worker_aio_requests"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_epoll_conf_t, aio_requests),
      NULL },

      ngx_null_command
};


static ngx_event_module_t  ngx_epoll_module_ctx = {
    &epoll_name,
    ngx_epoll_create_conf,               /* create configuration */
    ngx_epoll_init_conf,                 /* init configuration */

    {
        ngx_epoll_add_event,             /* add an event */
        ngx_epoll_del_event,             /* delete an event */
        ngx_epoll_add_event,             /* enable an event */
        ngx_epoll_del_event,             /* disable an event */
        ngx_epoll_add_connection,        /* add an connection */
        ngx_epoll_del_connection,        /* delete an connection */
#if (NGX_HAVE_EVENTFD)
        ngx_epoll_notify,                /* trigger a notify */
#else
        NULL,                            /* trigger a notify */
#endif
        ngx_epoll_process_events,        /* process the events */
        ngx_epoll_init,                  /* init the events */
        ngx_epoll_done,                  /* done the events */
    }
};

ngx_module_t  ngx_epoll_module = {
    NGX_MODULE_V1,
    &ngx_epoll_module_ctx,               /* module context */
    ngx_epoll_commands,                  /* module directives */
    NGX_EVENT_MODULE,                    /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


#if (NGX_HAVE_FILE_AIO)

/*
 * We call io_setup(), io_destroy() io_submit(), and io_getevents() directly
 * as syscalls instead of libaio usage, because the library header file
 * supports eventfd() since 0.3.107 version only.
 */

static int
io_setup(u_int nr_reqs, aio_context_t *ctx)
{
    return syscall(SYS_io_setup, nr_reqs, ctx);
}


static int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}


static int
io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events,
    struct timespec *tmo)
{
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, tmo);
}


static void
ngx_epoll_aio_init(ngx_cycle_t *cycle, ngx_epoll_conf_t *epcf)
{
    int                 n;
    struct epoll_event  ee;

#if (NGX_HAVE_SYS_EVENTFD_H)
    ngx_eventfd = eventfd(0, 0);
#else
    ngx_eventfd = syscall(SYS_eventfd, 0);
#endif

    if (ngx_eventfd == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "eventfd() failed");
        ngx_file_aio = 0;
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "eventfd: %d", ngx_eventfd);

    n = 1;

    if (ioctl(ngx_eventfd, FIONBIO, &n) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "ioctl(eventfd, FIONBIO) failed");
        goto failed;
    }

    if (io_setup(epcf->aio_requests, &ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "io_setup() failed");
        goto failed;
    }

    ngx_eventfd_event.data = &ngx_eventfd_conn;
    ngx_eventfd_event.handler = ngx_epoll_eventfd_handler;
    ngx_eventfd_event.log = cycle->log;
    ngx_eventfd_event.active = 1;
    ngx_eventfd_conn.fd = ngx_eventfd;
    ngx_eventfd_conn.read = &ngx_eventfd_event;
    ngx_eventfd_conn.log = cycle->log;

    ee.events = EPOLLIN|EPOLLET;
    ee.data.ptr = &ngx_eventfd_conn;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, ngx_eventfd, &ee) != -1) {
        return;
    }

    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                  "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

    if (io_destroy(ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "io_destroy() failed");
    }

failed:

    if (close(ngx_eventfd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    ngx_eventfd = -1;
    ngx_aio_ctx = 0;
    ngx_file_aio = 0;
}

#endif


static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    ngx_epoll_conf_t  *epcf;

    /* 获取配置项结构体 */
    epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);

    if (ep == -1) {
        /* 创建一个 epoll 句柄 */
        ep = epoll_create(cycle->connection_n / 2);

        if (ep == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "epoll_create() failed");
            return NGX_ERROR;
        }

#if (NGX_HAVE_EVENTFD)
        if (ngx_epoll_notify_init(cycle->log) != NGX_OK) {
            ngx_epoll_module_ctx.actions.notify = NULL;
        }
#endif

#if (NGX_HAVE_FILE_AIO)
        ngx_epoll_aio_init(cycle, epcf);
#endif

#if (NGX_HAVE_EPOLLRDHUP)
        ngx_epoll_test_rdhup(cycle);
#endif
    }

    if (nevents < epcf->events) {
        if (event_list) {
            ngx_free(event_list);
        }

        /* 创建 event_list 数组，用于进行 epoll_wait 调用时传递内核态的事件 */
        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,
                               cycle->log);
        if (event_list == NULL) {
            return NGX_ERROR;
        }
    }

    /* 将配置项epoll_events的参数赋给nevents */
    nevents = epcf->events;

    /* 指明读写I/O的方法 */
    ngx_io = ngx_os_io;

    /* 一旦设定Nginx使用某个事件处理模块，经过事件处理模块的初始化函数后，就把全局变量
     * ngx_event_actions指向了该模块的封装 */
    ngx_event_actions = ngx_epoll_module_ctx.actions;

#if (NGX_HAVE_CLEAR_EVENT)
    /* 默认是采用ET模式来使用epoll的，NGX_USE_CLEAR_EVENT宏实际上就是在告诉Nginx使用ET模式 */
    ngx_event_flags = NGX_USE_CLEAR_EVENT
#else
    ngx_event_flags = NGX_USE_LEVEL_EVENT
#endif
                      |NGX_USE_GREEDY_EVENT
                      |NGX_USE_EPOLL_EVENT;

    return NGX_OK;
}


#if (NGX_HAVE_EVENTFD)

static ngx_int_t
ngx_epoll_notify_init(ngx_log_t *log)
{
    struct epoll_event  ee;

#if (NGX_HAVE_SYS_EVENTFD_H)
    notify_fd = eventfd(0, 0);
#else
    notify_fd = syscall(SYS_eventfd, 0);
#endif

    if (notify_fd == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "eventfd() failed");
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0,
                   "notify eventfd: %d", notify_fd);

    notify_event.handler = ngx_epoll_notify_handler;
    notify_event.log = log;
    notify_event.active = 1;

    notify_conn.fd = notify_fd;
    notify_conn.read = &notify_event;
    notify_conn.log = log;

    ee.events = EPOLLIN|EPOLLET;
    ee.data.ptr = &notify_conn;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, notify_fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

        if (close(notify_fd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                            "eventfd close() failed");
        }

        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_epoll_notify_handler(ngx_event_t *ev)
{
    ssize_t               n;
    uint64_t              count;
    ngx_err_t             err;
    ngx_event_handler_pt  handler;

    if (++ev->index == NGX_MAX_UINT32_VALUE) {
        ev->index = 0;

        n = read(notify_fd, &count, sizeof(uint64_t));

        err = ngx_errno;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "read() eventfd %d: %z count:%uL", notify_fd, n, count);

        if ((size_t) n != sizeof(uint64_t)) {
            ngx_log_error(NGX_LOG_ALERT, ev->log, err,
                          "read() eventfd %d failed", notify_fd);
        }
    }

    handler = ev->data;
    handler(ev);
}

#endif


#if (NGX_HAVE_EPOLLRDHUP)

static void
ngx_epoll_test_rdhup(ngx_cycle_t *cycle)
{
    int                 s[2], events;
    struct epoll_event  ee;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "socketpair() failed");
        return;
    }

    ee.events = EPOLLET|EPOLLIN|EPOLLRDHUP;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, s[0], &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll_ctl() failed");
        goto failed;
    }

    if (close(s[1]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
        s[1] = -1;
        goto failed;
    }

    s[1] = -1;

    events = epoll_wait(ep, &ee, 1, 5000);

    if (events == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll_wait() failed");
        goto failed;
    }

    if (events) {
        ngx_use_epoll_rdhup = ee.events & EPOLLRDHUP;

    } else {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, NGX_ETIMEDOUT,
                      "epoll_wait() timed out");
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "testing the EPOLLRDHUP flag: %s",
                   ngx_use_epoll_rdhup ? "success" : "fail");

failed:

    if (s[1] != -1 && close(s[1]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
    }

    if (close(s[0]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() failed");
    }
}

#endif


static void
ngx_epoll_done(ngx_cycle_t *cycle)
{
    if (close(ep) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll close() failed");
    }

    ep = -1;

#if (NGX_HAVE_EVENTFD)

    if (close(notify_fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    notify_fd = -1;

#endif

#if (NGX_HAVE_FILE_AIO)

    if (ngx_eventfd != -1) {

        if (io_destroy(ngx_aio_ctx) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "io_destroy() failed");
        }

        if (close(ngx_eventfd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "eventfd close() failed");
        }

        ngx_eventfd = -1;
    }

    ngx_aio_ctx = 0;

#endif

    ngx_free(event_list);

    event_list = NULL;
    nevents = 0;
}


static ngx_int_t
ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             events, prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;

    /* 每个事件的 data 成员都存放着其对应的 ngx_connection_t 连接 */
    c = ev->data;

    /* 下面会根据event参数确定当前事件时读事件还是写事件，
     * 这会决定events是加上EPOLLIN标志还是EPOLLOUT标志位 */
    events = (uint32_t) event;

    if (event == NGX_READ_EVENT) {
        e = c->write;
        prev = EPOLLOUT;
#if (NGX_READ_EVENT != EPOLLIN|EPOLLRDHUP)
        events = EPOLLIN|EPOLLRDHUP;
#endif

    } else {
        e = c->read;
        prev = EPOLLIN|EPOLLRDHUP;
#if (NGX_WRITE_EVENT != EPOLLOUT)
        events = EPOLLOUT;
#endif
    }

    /* 根据active标志位确定是否为活跃事件，以决定到底是修改还是添加事件 */
    if (e->active) {
        op = EPOLL_CTL_MOD;
        events |= prev;

    } else {
        op = EPOLL_CTL_ADD;
    }

#if (NGX_HAVE_EPOLLEXCLUSIVE && NGX_HAVE_EPOLLRDHUP)
    if (flags & NGX_EXCLUSIVE_EVENT) {
        events &= ~EPOLLRDHUP;
    }
#endif

    /* 加入flags参数到events标志位中 */
    ee.events = events | (uint32_t) flags;
    /* ptr成员存储的是ngx_connection_t连接 */
    ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll add event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    /* 调用 epoll_ctl 方法向 epoll 中添加事件或者在 epoll 中修改事件 */
    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    /* 将事件的 active 标志位置为 1，表示当前事件是活跃的 */
    ev->active = 1;
#if 0
    ev->oneshot = (flags & NGX_ONESHOT_EVENT) ? 1 : 0;
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;

    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        ev->active = 0;
        return NGX_OK;
    }

    c = ev->data;

    if (event == NGX_READ_EVENT) {
        e = c->write;
        prev = EPOLLOUT;

    } else {
        e = c->read;
        prev = EPOLLIN|EPOLLRDHUP;
    }

    if (e->active) {
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t) flags;
        ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    } else {
        op = EPOLL_CTL_DEL;
        ee.events = 0;
        ee.data.ptr = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll del event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    ev->active = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c)
{
    struct epoll_event  ee;

    ee.events = EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP;
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll add connection: fd:%d ev:%08XD", c->fd, ee.events);

    if (epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
        return NGX_ERROR;
    }

    c->read->active = 1;
    c->write->active = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    int                 op;
    struct epoll_event  ee;

    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll del connection: fd:%d", c->fd);

    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = NULL;

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}


#if (NGX_HAVE_EVENTFD)

static ngx_int_t
ngx_epoll_notify(ngx_event_handler_pt handler)
{
    static uint64_t inc = 1;

    notify_event.data = handler;

    if ((size_t) write(notify_fd, &inc, sizeof(uint64_t)) != sizeof(uint64_t)) {
        ngx_log_error(NGX_LOG_ALERT, notify_event.log, ngx_errno,
                      "write() to eventfd %d failed", notify_fd);
        return NGX_ERROR;
    }

    return NGX_OK;
}

#endif


/*ngx_epoll_process_events 方法会收集当前触发的所有事件，对于不需要加入到 post 队列延后处理的事件，该方法会立即执行
它们的回调方法，这其实是在做分发事件的工作，只是它会在自己的进程中调用这些回调方法而已，因此，每一个回调方法都不能
导致进程休眠或者消耗太多时间，以免 epoll 不能即时地处理其他事件*/
static ngx_int_t
ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    int                events;
    uint32_t           revents;
    ngx_int_t          instance, i;
    ngx_uint_t         level;
    ngx_err_t          err;
    ngx_event_t       *rev, *wev;
    ngx_queue_t       *queue;
    ngx_connection_t  *c;

    /* NGX_TIMER_INFINITE == INFTIM */

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "epoll timer: %M", timer);

    /* 调用 epoll_wait 获取事件。注意，timer 参数是在 process_events 调用时传入的 */
    /* nginx 最初运行时，timer 为 -1，即一直等待客户端连接 */
    events = epoll_wait(ep, event_list, (int) nevents, timer);

    err = (events == -1) ? ngx_errno : 0;

    /* 判断flags标志位是否指示要更新时间或ngx_event_timer_alarm是否为1 */
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
        ngx_time_update();
    }

    if (err) {
        if (err == NGX_EINTR) {

            if (ngx_event_timer_alarm) {
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }

            level = NGX_LOG_INFO;

        } else {
            level = NGX_LOG_ALERT;
        }

        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

    if (events == 0) {
        if (timer != NGX_TIMER_INFINITE) {
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }

    /* 遍历本次 epoll_wait 返回的所有事件 */
    for (i = 0; i < events; i++) {
        c = event_list[i].data.ptr;

        /* uintptr_t 在 64 位平台上为：typedef unsigned long int upintptr_t;
         *           在 32 位平台上为：typedef unsigned int uintptr_t; 
         * 该类型主要是为了跨平台，其长度总是所在平台的位数，常用于存放地址. */

        /* 将地址的最后一位取出来，用 instance 变量标识 */
        instance = (uintptr_t) c & 1;
        /* 无论是 32 位还是 64 位机器，其地址的最后 1 位肯定是 0，可以用下面这行语句把
         * ngx_connection_t 的地址还原到真正的地址值 */
        c = (ngx_connection_t *) ((uintptr_t) c & (uintptr_t) ~1);

        /* 获取被监听的读事件 */
        rev = c->read;

        /* 判断这个读事件是否是过期事件 */
        if (c->fd == -1 || rev->instance != instance) {

            /* 当 fd 套接字描述符为 -1 或者 instance 标志位不相等时，
             * 表示这个事件已经过期了，不用处理 */
            
            /*
             * the stale event from a file descriptor
             * that was just closed in this iteration
             */

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll: stale event %p", c);
            continue;
        }

        /* 取出事件类型 */  /* 获取 epoll_wait 返回的事件标志 */
        revents = event_list[i].events;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "epoll: fd:%d ev:%04XD d:%p",
                       c->fd, revents, event_list[i].data.ptr);

        /* 若事件发生错误或被挂起 */
        if (revents & (EPOLLERR|EPOLLHUP)) {
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll_wait() error on fd:%d ev:%04XD",
                           c->fd, revents);

            /*
             * if the error events were returned, add EPOLLIN and EPOLLOUT
             * to handle the events at least in one active handler
             */

            revents |= EPOLLIN|EPOLLOUT;
        }

#if 0
        if (revents & ~(EPOLLIN|EPOLLOUT|EPOLLERR|EPOLLHUP)) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "strange epoll_wait() events fd:%d ev:%04XD",
                          c->fd, revents);
        }
#endif

        /* 如果是读事件且该事件是活跃的 */  /* 若是监听的事件可读，首次监听即表示有新连接到来 */
        if ((revents & EPOLLIN) && rev->active) {

#if (NGX_HAVE_EPOLLRDHUP)
            if (revents & EPOLLRDHUP) {
                rev->pending_eof = 1;
            }
#endif

            rev->ready = 1;
            rev->available = -1;

            /* 若是开启了负载均衡，则先将该事件添加到 ngx_posted_accept_events 
             * 延迟队列中 */ /* flags 参数中含有 NGX_POST_EVENTS 表示这批事件要延后处理 */
            if (flags & NGX_POST_EVENTS) {
                /* 如果要在 post 队列中延后处理该事件，首先要判断它是新连接事件还是普通事件，
                 * 以决定把它加入到 ngx_posted_accept_events 队列或者 ngx_posted_events 队列
                 * 中 */
                queue = rev->accept ? &ngx_posted_accept_events
                                    : &ngx_posted_events;

                /* 将这个事件添加到相应的延后执行队列中 */
                ngx_post_event(rev, queue);

            } else {
                /* 否则，直接调用该读事件的回调函数，若是新连接则
                 * 调用的是 ngx_event_accept 函数 */
                rev->handler(rev);
            }
        }

        /* 取出写事件 */
        wev = c->write;

        /* 如果写事件且事件是活跃的 */
        if ((revents & EPOLLOUT) && wev->active) {

            /* 检测该事件是否是过期的 */
            if (c->fd == -1 || wev->instance != instance) {

                /* 如果 fd 描述符为 -1 或者 instance 标志位不相等时，表示这个
                 * 事件是过期的，不用处理 */
                
                /*
                 * the stale event from a file descriptor
                 * that was just closed in this iteration
                 */

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                               "epoll: stale event %p", c);
                continue;
            }

            wev->ready = 1;
#if (NGX_THREADS)
            wev->complete = 1;
#endif

            /* 若该事件要延迟处理 */
            if (flags & NGX_POST_EVENTS) {
                /* 将该写事件添加到延迟执行队列中 */
                ngx_post_event(wev, &ngx_posted_events);

            } else {
                /* 立即调用这个写事件的回调方法来处理这个事件 */
                wev->handler(wev);
            }
        }
    }

    return NGX_OK;
}


#if (NGX_HAVE_FILE_AIO)

static void
ngx_epoll_eventfd_handler(ngx_event_t *ev)
{
    int               n, events;
    long              i;
    uint64_t          ready;
    ngx_err_t         err;
    ngx_event_t      *e;
    ngx_event_aio_t  *aio;
    struct io_event   event[64];
    struct timespec   ts;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd handler");

    n = read(ngx_eventfd, &ready, 8);

    err = ngx_errno;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd: %d", n);

    if (n != 8) {
        if (n == -1) {
            if (err == NGX_EAGAIN) {
                return;
            }

            ngx_log_error(NGX_LOG_ALERT, ev->log, err, "read(eventfd) failed");
            return;
        }

        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "read(eventfd) returned only %d bytes", n);
        return;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 0;

    while (ready) {

        events = io_getevents(ngx_aio_ctx, 1, 64, event, &ts);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "io_getevents: %d", events);

        if (events > 0) {
            ready -= events;

            for (i = 0; i < events; i++) {

                ngx_log_debug4(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "io_event: %XL %XL %L %L",
                                event[i].data, event[i].obj,
                                event[i].res, event[i].res2);

                e = (ngx_event_t *) (uintptr_t) event[i].data;

                e->complete = 1;
                e->active = 0;
                e->ready = 1;

                aio = e->data;
                aio->res = event[i].res;

                ngx_post_event(e, &ngx_posted_events);
            }

            continue;
        }

        if (events == 0) {
            return;
        }

        /* events == -1 */
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "io_getevents() failed");
        return;
    }
}

#endif


static void *
ngx_epoll_create_conf(ngx_cycle_t *cycle)
{
    ngx_epoll_conf_t  *epcf;

    epcf = ngx_palloc(cycle->pool, sizeof(ngx_epoll_conf_t));
    if (epcf == NULL) {
        return NULL;
    }

    epcf->events = NGX_CONF_UNSET;
    epcf->aio_requests = NGX_CONF_UNSET;

    return epcf;
}


static char *
ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_epoll_conf_t *epcf = conf;

    ngx_conf_init_uint_value(epcf->events, 512);
    ngx_conf_init_uint_value(epcf->aio_requests, 32);

    return NGX_CONF_OK;
}
