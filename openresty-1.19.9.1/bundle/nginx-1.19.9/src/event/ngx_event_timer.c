
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


/* Nginx设置了两个全局变量以便在程序的任何地方都可以快速地访问到这颗红黑树 */
/* ngx_event_timer_rbtree封装了整颗红黑树结构 */
ngx_rbtree_t              ngx_event_timer_rbtree;
/* ngx_event_timer_sentinel属于红黑树节点类型变量，在红黑树的操作过程中被当做哨兵节点使用 */
static ngx_rbtree_node_t  ngx_event_timer_sentinel;
/*这棵红黑树中的每个节点都是 ngx_event_t 事件中的 timer 成员，而 ngx_rbtree_node_t 节点的关键字就是事件的超时
时间，以这个超时时间的大小组成了二叉排序树 ngx_event_timer_rbtree。这样，如果需要找出最有可能超时的事件，那
么将 ngx_event_timer_rbtree 树中最左边的节点取出来即可。只要用当前时间去比较这个最左边节点的超时时间，就会
知道这个事件有没有触发超时，如果还没有触发超时，那么会知道最少还要经过多少毫秒满足超时条件而触发超时。*/

/*
 * the event timer rbtree may contain the duplicate keys, however,
 * it should not be a problem, because we use the rbtree to find
 * a minimum timer value only
 */

/* 红黑树(即定时器)的初始化函数 */
ngx_int_t
ngx_event_timer_init(ngx_log_t *log)
{
    /* ngx_event_timer_rbtree 和 ngx_event_timer_sentinel 是两个全局变量，前者指向
     * 整颗红黑树，后者指向了哨兵节点, ngx_rbtree_insert_timer_value 函数指针则为
     * 将元素插入这棵红黑树的方法 */
    ngx_rbtree_init(&ngx_event_timer_rbtree, &ngx_event_timer_sentinel,
                    ngx_rbtree_insert_timer_value);

    return NGX_OK;
}


/*
 * 执行意义：
 * 找出红黑树中最左边的节点，如果它的超时时间大于当前时间，也就表明目前的定时器中没有一个事件
 * 满足触发条件，这时返回这个超时与当前时间的差值，也就是需要经过多少毫秒会有事件超时触发；如果
 * 它的超时时间小于或等于当前时间，则返回0，表示定时器中已经存在超时需要触发的事件.
 */
ngx_msec_t
ngx_event_find_timer(void)
{
    ngx_msec_int_t      timer;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    /* 检测红黑树是否为空 */
    if (ngx_event_timer_rbtree.root == &ngx_event_timer_sentinel) {
        return NGX_TIMER_INFINITE;
    }

    root = ngx_event_timer_rbtree.root;
    sentinel = ngx_event_timer_rbtree.sentinel;

    /* 找出该树 key 最小的那个节点，即超时时间最小的 */
    node = ngx_rbtree_min(root, sentinel);

    /* 该节点的超时时间与当前时间的毫秒比较，若大于，则表明还没有触发超时，返回它们的差值；
     * 若小于或等于，则表示已经满足超时条件，返回0 */
    timer = (ngx_msec_int_t) (node->key - ngx_current_msec);

    return (ngx_msec_t) (timer > 0 ? timer : 0);
}


/*
 * 执行意义：
 * 检查定时器中的所有事件，按照红黑树关键字由小到大的顺序依次调用已经满足
 * 超时条件的需要被触发事件的 handler 回调方法.
 */
void
ngx_event_expire_timers(void)
{
    ngx_event_t        *ev;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    sentinel = ngx_event_timer_rbtree.sentinel;

    for ( ;; ) {
        root = ngx_event_timer_rbtree.root;

        if (root == sentinel) {
            return;
        }

        node = ngx_rbtree_min(root, sentinel);

        /* node->key > ngx_current_msec */

        /* 没有超时，则直接返回 */
        if ((ngx_msec_int_t) (node->key - ngx_current_msec) > 0) {
            return;
        }

        /* 计算 ev 的首地址位置 */
        ev = (ngx_event_t *) ((char *) node - offsetof(ngx_event_t, timer));

        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "event timer del: %d: %M",
                       ngx_event_ident(ev->data), ev->timer.key);

        /* 该事件已经满足超时条件，需要从定时器中移除 */
        ngx_rbtree_delete(&ngx_event_timer_rbtree, &ev->timer);

#if (NGX_DEBUG)
        ev->timer.left = NULL;
        ev->timer.right = NULL;
        ev->timer.parent = NULL;
#endif

        /* 置为 0，表示已经不在定时器中了 */
        ev->timer_set = 0;

        /* 置为 1，表示已经超时了 */
        ev->timedout = 1;

        /* 调用该超时事件的方法 */
        ev->handler(ev);
    }
}


ngx_int_t
ngx_event_no_timers_left(void)
{
    ngx_event_t        *ev;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    sentinel = ngx_event_timer_rbtree.sentinel;
    root = ngx_event_timer_rbtree.root;

    if (root == sentinel) {
        return NGX_OK;
    }

    for (node = ngx_rbtree_min(root, sentinel);
         node;
         node = ngx_rbtree_next(&ngx_event_timer_rbtree, node))
    {
        ev = (ngx_event_t *) ((char *) node - offsetof(ngx_event_t, timer));

        if (!ev->cancelable) {
            return NGX_AGAIN;
        }
    }

    /* only cancelable timers left */

    return NGX_OK;
}
