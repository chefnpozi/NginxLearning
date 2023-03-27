
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_RBTREE_H_INCLUDED_
#define _NGX_RBTREE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef ngx_uint_t  ngx_rbtree_key_t;
typedef ngx_int_t   ngx_rbtree_key_int_t;


typedef struct ngx_rbtree_node_s  ngx_rbtree_node_t;

struct ngx_rbtree_node_s {
    /* 无符号整型的关键字 */
    ngx_rbtree_key_t       key;
    /* 左子节点 */
    ngx_rbtree_node_t     *left;
    /* 右子节点 */
    ngx_rbtree_node_t     *right;
    /* 父节点 */
    ngx_rbtree_node_t     *parent;
    /* 节点的颜色，0 表示黑色，1 表示红色 */
    u_char                 color;
    /* 仅 1 字节的节点数据。由于表示的空间太小，一般很少使用 */
    u_char                 data;

    /*ngx_rbtree_node_t 是红黑树实现中必须用到的数据结构，一般把它放到结构体中的第 1 个成员中，这样方便把自定义的结
    构体强制成 ngx_rbtree_node_t 类型。
    ngx_rbtree_node_t 结构体中的 key 成员是每个红黑树节点的关键字，它必须是整型。红黑树的排序主要依据 key 成员
    （自定义 ngx_rbtree_insert_pt 方法后，节点的其他成员也可以在 key 排序的基础上影响红黑树的形态）。*/
};


typedef struct ngx_rbtree_s  ngx_rbtree_t;

/* 为解决不同节点含有相同关键字的元素冲突问题，红黑树设置了 ngx_rbtree_insert_pt 
 * 指针，这样可灵活地添加冲突元素 */
typedef void (*ngx_rbtree_insert_pt) (ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);

/*红黑树是一个通用的数据结构，它的节点（或者称为容器的元素）可以是包含基本红黑树节点的任意结构体。对于不同的结
构体，很多场合下是允许不同的节点拥有相同的关键字的。例如，不同的字符串可能会散列出相同的关键字，这时它们在红
黑树中的关键字是相同的，然而它们又是不同的节点，这样在添加时就不可以覆盖原有同名关键字的节点，而是作为新插入
的节点存在。因此，将添加元素的方法抽象出 ngx_rbtree_insert_pt 函数指针可以很好地实现这一思想。*/
struct ngx_rbtree_s {
    /* 指树的根节点。注意，根节点也是数据元素 */
    ngx_rbtree_node_t     *root;
    /* 指向 NIL 哨兵节点 */
    ngx_rbtree_node_t     *sentinel;
    /* 表示红黑树添加元素的函数指针，它决定在添加新节点时的行为究竟是替换还是新增 */
    ngx_rbtree_insert_pt   insert;
};


/*
 * 参数含义：
 * - tree：是红黑树容器的指针
 * - s：是哨兵节点的指针
 * - i：是ngx_rbtree_insert_pt类型的节点添加方法
 *
 * 执行意义：
 * 初始化红黑树，包括初始化根节点、哨兵节点、ngx_rbtree_insert_pt节点添加方法
 */
#define ngx_rbtree_init(tree, s, i)                                           \
    ngx_rbtree_sentinel_init(s);                                              \
    (tree)->root = s;                                                         \
    (tree)->sentinel = s;                                                     \
    (tree)->insert = i


void ngx_rbtree_insert(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
void ngx_rbtree_delete(ngx_rbtree_t *tree, ngx_rbtree_node_t *node);
void ngx_rbtree_insert_value(ngx_rbtree_node_t *root, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel);
void ngx_rbtree_insert_timer_value(ngx_rbtree_node_t *root,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
ngx_rbtree_node_t *ngx_rbtree_next(ngx_rbtree_t *tree,
    ngx_rbtree_node_t *node);


/* 设置该节点颜色为红色 */
#define ngx_rbt_red(node)               ((node)->color = 1)
/* 设置该节点颜色为黑色 */
#define ngx_rbt_black(node)             ((node)->color = 0)
#define ngx_rbt_is_red(node)            ((node)->color)
#define ngx_rbt_is_black(node)          (!ngx_rbt_is_red(node))
#define ngx_rbt_copy_color(n1, n2)      (n1->color = n2->color)


/* a sentinel must be black */
/* 初始化一个哨兵节点，哨兵节点（即叶子节点）一定是黑色的 */
#define ngx_rbtree_sentinel_init(node)  ngx_rbt_black(node)


/*
 * 参数含义：
 * - node：是红黑树中ngx_rbtree_node_t类型的节点指针
 * - sentinel：是这棵红黑树的哨兵节点
 * 
 * 执行意义：
 * 找到当前节点及其子树中最小节点(按照key关键字)
 */
static ngx_inline ngx_rbtree_node_t *
ngx_rbtree_min(ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    while (node->left != sentinel) {
        node = node->left;
    }

    return node;
}


#endif /* _NGX_RBTREE_H_INCLUDED_ */
