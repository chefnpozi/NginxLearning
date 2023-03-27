
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_EVENT_TIMER_H_INCLUDED_
#define _NGX_EVENT_TIMER_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define NGX_TIMER_INFINITE  (ngx_msec_t) -1

#define NGX_TIMER_LAZY_DELAY  300


ngx_int_t ngx_event_timer_init(ngx_log_t *log);
ngx_msec_t ngx_event_find_timer(void);
void ngx_event_expire_timers(void);
ngx_int_t ngx_event_no_timers_left(void);


extern ngx_rbtree_t  ngx_event_timer_rbtree;


static ngx_inline void
ngx_event_del_timer(ngx_event_t *ev)
{
    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "event timer del: %d: %M",
                    ngx_event_ident(ev->data), ev->timer.key);

    ngx_rbtree_delete(&ngx_event_timer_rbtree, &ev->timer);

#if (NGX_DEBUG)
    ev->timer.left = NULL;
    ev->timer.right = NULL;
    ev->timer.parent = NULL;
#endif

    ev->timer_set = 0;
}


/*
 * 参数含义：
 * - ev：是需要操作的事件
 * - timer：单位是毫秒，它告诉定时器事件ev希望timer毫秒后超时，同时需要回调ev的handler方法
 *
 * 执行意义：
 * 添加一个定时器事件，超时时间为 timer 毫秒
 */
static ngx_inline void
ngx_event_add_timer(ngx_event_t *ev, ngx_msec_t timer)
{
    ngx_msec_t      key;
    ngx_msec_int_t  diff;

    key = ngx_current_msec + timer;

    /* 若该事件已经添加到红黑树中 */
    if (ev->timer_set) {

        /*
         * Use a previous timer value if difference between it and a new
         * value is less than NGX_TIMER_LAZY_DELAY milliseconds: this allows
         * to minimize the rbtree operations for fast connections.
         */

        diff = (ngx_msec_int_t) (key - ev->timer.key);

        if (ngx_abs(diff) < NGX_TIMER_LAZY_DELAY) {
            ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                           "event timer: %d, old: %M, new: %M",
                            ngx_event_ident(ev->data), ev->timer.key, key);
            return;
        }

        /* 将该事件从红黑树中删除 */
        ngx_del_timer(ev);
    }

    /* 记录该事件的超时时刻，在后续进行超时检测扫描时需要该字段来进行时刻的先后比较 */
    ev->timer.key = key;

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "event timer add: %d: %M:%M",
                    ngx_event_ident(ev->data), timer, ev->timer.key);

    /* 将事件添加到红黑树中
     * 这种添加是间接性的，每个事件对象封装结构体中都有一个timer字段，
     * 其为ngx_rbtree_node_t 类型变量，加入到红黑树中就是该字段，
     * 而非事件对象结构体本身。后面要获取该事件结构体时可以通过利用
     * offsetof宏来根据该timer字段方便找到其所在的对应事件对象结构体. */
    ngx_rbtree_insert(&ngx_event_timer_rbtree, &ev->timer);

    /* 置位该变量，表示添加到红黑树中 */
    ev->timer_set = 1;
}


#endif /* _NGX_EVENT_TIMER_H_INCLUDED_ */
