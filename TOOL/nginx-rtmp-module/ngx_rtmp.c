
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>
#include "ngx_rtmp.h"


static char *ngx_rtmp_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_rtmp_add_ports(ngx_conf_t *cf, ngx_array_t *ports,
    ngx_rtmp_listen_t *listen);
static char *ngx_rtmp_optimize_servers(ngx_conf_t *cf, ngx_array_t *ports);
static ngx_int_t ngx_rtmp_add_addrs(ngx_conf_t *cf, ngx_rtmp_port_t *mport,
    ngx_rtmp_conf_addr_t *addr);
#if (NGX_HAVE_INET6)
static ngx_int_t ngx_rtmp_add_addrs6(ngx_conf_t *cf, ngx_rtmp_port_t *mport,
    ngx_rtmp_conf_addr_t *addr);
#endif
static ngx_int_t ngx_rtmp_cmp_conf_addrs(const void *one, const void *two);
static ngx_int_t ngx_rtmp_init_events(ngx_conf_t *cf,
        ngx_rtmp_core_main_conf_t *cmcf);
static ngx_int_t ngx_rtmp_init_event_handlers(ngx_conf_t *cf,
        ngx_rtmp_core_main_conf_t *cmcf);
static char * ngx_rtmp_merge_applications(ngx_conf_t *cf,
        ngx_array_t *applications, void **app_conf, ngx_rtmp_module_t *module,
        ngx_uint_t ctx_index);
static ngx_int_t ngx_rtmp_init_process(ngx_cycle_t *cycle);


#if (nginx_version >= 1007011)
ngx_queue_t                         ngx_rtmp_init_queue;
#elif (nginx_version >= 1007005)
ngx_thread_volatile ngx_queue_t     ngx_rtmp_init_queue;
#else
ngx_thread_volatile ngx_event_t    *ngx_rtmp_init_queue;
#endif


ngx_uint_t  ngx_rtmp_max_module;

/*ngx_rtmp_module 核心模块仅是当 nginx.conf 中配置了 rtmp{} 块时指定了 ngx_rtmp_block 方法去解析*/
static ngx_command_t  ngx_rtmp_commands[] = {

    { ngx_string("rtmp"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_rtmp_block,
      0,
      0,
      NULL },

      ngx_null_command
};

// 这里仅定义了该核心模块的名字
static ngx_core_module_t  ngx_rtmp_module_ctx = {
    ngx_string("rtmp"),
    NULL,
    NULL
};

/* 模块的定义: 该结构体中仅实现了 init process 方法，该方法也仅是初始化了一个 ngx_rtmp_init_queue 的双向链表 */
ngx_module_t  ngx_rtmp_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_module_ctx,                  /* module context */
    ngx_rtmp_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_rtmp_init_process,                 /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_rtmp_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                        *rv;
    ngx_uint_t                   i, m, mi, s;
    ngx_conf_t                   pcf;
    ngx_array_t                  ports;
    ngx_module_t               **modules;
    ngx_rtmp_listen_t           *listen;
    ngx_rtmp_module_t           *module;
    ngx_rtmp_conf_ctx_t         *ctx;
    ngx_rtmp_core_srv_conf_t    *cscf, **cscfp;
    ngx_rtmp_core_main_conf_t   *cmcf;

    /* 为存放 rtmp{} 块内的配置项分配一个上下文结构体，该结构体中管理着所有
     * rtmp{} 块内的配置项 */
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    /* 将该结构体指针赋给核心结构体 ngx_cycle_t 的成员 conf_ctx 指针数组中的相应位置  */
    *(ngx_rtmp_conf_ctx_t **)conf = ctx;

    /* count the number of the rtmp modules and set up their indices */

#if (nginx_version >= 1009011)

    ngx_rtmp_max_module = ngx_count_modules(cf->cycle, NGX_RTMP_MODULE);

#else

    /* 遍历所有的 RTMP 模块，初始化每个 RTMP 的 ctx_index 索引  */
    ngx_rtmp_max_module = 0;
    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_RTMP_MODULE) {
            continue;
        }

        ngx_modules[m]->ctx_index = ngx_rtmp_max_module++;
    }

#endif

    /* the rtmp main_conf context, it is the same in the all rtmp contexts */
    /* 为所有 RTMP 模块都创建一个保存 rtmp{} 块下的 main 级别的配置项的指针数组 */
    ctx->main_conf = ngx_pcalloc(cf->pool,
                                 sizeof(void *) * ngx_rtmp_max_module);
    if (ctx->main_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    /*
     * the rtmp null srv_conf context, it is used to merge
     * the server{}s' srv_conf's
     */
    /* 为所有 RTMP 模块都创建一个保存 rtmp{} 块下的 srv 级别的配置项的指针数组 */
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_rtmp_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    /*
     * the rtmp null app_conf context, it is used to merge
     * the server{}s' app_conf's
     */
    /* 为所有 RTMP 模块都创建一个保存 rtmp{} 块下的 app 级别的配置项的指针数组 */
    ctx->app_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_rtmp_max_module);
    if (ctx->app_conf == NULL) {
        return NGX_CONF_ERROR;
    }

    /*
     * create the main_conf's, the null srv_conf's, and the null app_conf's
     * of the all rtmp modules
     */

#if (nginx_version >= 1009011)
    modules = cf->cycle->modules;
#else
    modules = ngx_modules;
#endif

    /* 遍历 Nginx 中所有的 RTMP 模块，调用各自 RTMP 模块实现的
     * create_main_conf、create_srv_conf、create_app_conf 方法 */
    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTMP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;
        mi = modules[m]->ctx_index;

        if (module->create_main_conf) {
            ctx->main_conf[mi] = module->create_main_conf(cf);
            if (ctx->main_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_srv_conf) {
            ctx->srv_conf[mi] = module->create_srv_conf(cf);
            if (ctx->srv_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_app_conf) {
            ctx->app_conf[mi] = module->create_app_conf(cf);
            if (ctx->app_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;

    /* 在解析配置项前，调用所有 RTMP 模块的 preconfiguration 方法
     * 在 nginx-rmtp-module 中，基本不需要实现此方法 */
    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTMP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;

        if (module->preconfiguration) {
            if (module->preconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    /* parse inside the rtmp{} block */
    /* 开始解析 rtmp{}  */
    cf->module_type = NGX_RTMP_MODULE;
    cf->cmd_type = NGX_RTMP_MAIN_CONF;
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        *cf = pcf;
        return rv;
    }

    /* init rtmp{} main_conf's, merge the server{}s' srv_conf's */
    /* 获取所属 rtmp{} 块下的第一个 RTMP 模块创建的 ngx_rtmp_core_main_conf_t 结构体 */
    cmcf = ctx->main_conf[ngx_rtmp_core_module.ctx_index];
    /* cmcf->servers 数组中保存着 rtmp{} 块下所有 server{} 块的
     * ngx_rtmp_core_srv_conf_t 结构体 */
    cscfp = cmcf->servers.elts;

    /* 遍历所有的 RTMP 模块 */
    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTMP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;
        mi = modules[m]->ctx_index;

        /* init rtmp{} main_conf's */

        cf->ctx = ctx;

        if (module->init_main_conf) {
            rv = module->init_main_conf(cf, ctx->main_conf[mi]);
            if (rv != NGX_CONF_OK) {
                *cf = pcf;
                return rv;
            }
        }

        for (s = 0; s < cmcf->servers.nelts; s++) {
            /* merge the server{}s' srv_conf's */

            cf->ctx = cscfp[s]->ctx;

            /* 合并 main 和 srv 这两个级别下出现同样配置项的值，合并规则为，
             * 1. 若 main 和 srv 级别下都无该配置项，则使用默认值；
             * 2. 若 srv 级别下有该配置项的值，而以 srv 级别下的为准；
             * 3. 若 srv 级别下没有而 mian 级别下有该配置项的值，则才使用 main 级别下的值 */
            if (module->merge_srv_conf) {
                rv = module->merge_srv_conf(cf,
                                            ctx->srv_conf[mi],
                                            cscfp[s]->ctx->srv_conf[mi]);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }
            }

            if (module->merge_app_conf) {
                /* merge the server{}'s app_conf */

                /*ctx->app_conf = cscfp[s]->ctx->loc_conf;*/

                rv = module->merge_app_conf(cf,
                                            ctx->app_conf[mi],
                                            cscfp[s]->ctx->app_conf[mi]);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }

                /* merge the applications{}' app_conf's */

                /* cscf 指向 server{} 级别下 ngx_rtmp_conf_ctx_t 结构体的
                 * srv_conf 指针数组的第 1 个元素(即 srv_conf[0])，也即
                 * 该 server{} 块对应的 ngx_rtmp_core_srv_conf_t 结构体 */
                cscf = cscfp[s]->ctx->srv_conf[ngx_rtmp_core_module.ctx_index];

                /* 合并 server 级别下同时出现在 srv 和 app 中的相同配置项.
                 *
                 * 参数含义：
                 * @ cf：指向全局的 ngx_conf_t 结构体指针
                 * @ cscf->applications: 指向该 server{} 块下所有 application{} 的数组首地址，
                 *          该数组每一个元素对应一个 application{}，即 ngx_rtmp_core_app_conf_t
                 * @ cscfp[s]->ctx->app_conf: 指向所属 server{} 块下的 ngx_rtmp_conf_ctx_t
                 *          结构体的 app_conf 指针数组
                 * @ module：rtmp 模块
                 * @ mi: 该 rtmp 模块在所有 RTMP 模块中的序号 */
                rv = ngx_rtmp_merge_applications(cf, &cscf->applications,
                                                 cscfp[s]->ctx->app_conf,
                                                 module, mi);
                if (rv != NGX_CONF_OK) {
                    *cf = pcf;
                    return rv;
                }
            }
        }
    }

    /* 初始化 ngx_rtmp_core_main_conf_t 结构体下的
     * events 数组和 amf 数组 */
    if (ngx_rtmp_init_events(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* 调用所有模块的 postconfiguration 方法 */
    for (m = 0; modules[m]; m++) {
        if (modules[m]->type != NGX_RTMP_MODULE) {
            continue;
        }

        module = modules[m]->ctx;

        /* 每个 rtmp 模块想要执行的动作都是通过该方法插入的 */
        if (module->postconfiguration) {
            if (module->postconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    *cf = pcf;

    /* 初始化接收到相应的rtmp消息时执行的回调函数 */
    if (ngx_rtmp_init_event_handlers(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (ngx_array_init(&ports, cf->temp_pool, 4, sizeof(ngx_rtmp_conf_port_t)) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /* cmcf->listen 数组保存着 rtmp{} 下所有需要监听的端口 */
    listen = cmcf->listen.elts;

    for (i = 0; i < cmcf->listen.nelts; i++) {
        if (ngx_rtmp_add_ports(cf, &ports, &listen[i]) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    /* 主要生成 ngx_listening_t 结构体 */
    return ngx_rtmp_optimize_servers(cf, &ports);
}


/* 合并 server 级别下同时出现在 srv 和 app 中的相同配置项.
 *
 * 参数含义：
 * @ cf：指向全局的 ngx_conf_t 结构体指针
 * @ applications: 指向该 server{} 块下所有 application{} 的数组首地址，
 *                 该数组每一个元素对应一个 application{}，即 ngx_rtmp_core_app_conf_t 
 * @ app_conf: 指向所属 server{} 块下的 ngx_rtmp_conf_ctx_t 结构体
 *             的 app_conf 指针数组
 * @ module：rtmp 模块
 * @ mi: 该 rtmp 模块在所有 RTMP 模块中的序号 */
static char *
ngx_rtmp_merge_applications(ngx_conf_t *cf, ngx_array_t *applications,
            void **app_conf, ngx_rtmp_module_t *module, ngx_uint_t ctx_index)
{
    char                           *rv;
    ngx_rtmp_conf_ctx_t            *ctx, saved;
    ngx_rtmp_core_app_conf_t      **cacfp;
    ngx_uint_t                      n;
    ngx_rtmp_core_app_conf_t       *cacf;

    /* 若该 server{} 块下没有 application{}，则直接返回 */
    if (applications == NULL) {
        return NGX_CONF_OK;
    }

    /* ctx 指向 rtmp{} 块下的 ngx_rtmp_conf_ctx_t 结构体 */
    ctx = (ngx_rtmp_conf_ctx_t *) cf->ctx;
    saved = *ctx;

    /* 遍历 server{} 块下 所有的 application{} 块 */
    cacfp = applications->elts;
    for (n = 0; n < applications->nelts; ++n, ++cacfp) {

        /* 先将该 application{} 块所属的 ngx_rtmp_conf_ctx_t 结构体的
         * app_conf 指针数组赋给 ctx->app_conf */
        ctx->app_conf = (*cacfp)->app_conf;

        /* 合并同时出现在 server{} 块下和 application{} 块下配置项 */
        rv = module->merge_app_conf(cf, app_conf[ctx_index],
                (*cacfp)->app_conf[ctx_index]);
        if (rv != NGX_CONF_OK) {
            return rv;
        }

        /* 这里是合并嵌套 application 的情况 */
        cacf = (*cacfp)->app_conf[ngx_rtmp_core_module.ctx_index];
        rv = ngx_rtmp_merge_applications(cf, &cacf->applications,
                                         (*cacfp)->app_conf,
                                         module, ctx_index);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
    }

    *ctx = saved;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_init_events(ngx_conf_t *cf, ngx_rtmp_core_main_conf_t *cmcf)
{
    size_t                      n;

    for(n = 0; n < NGX_RTMP_MAX_EVENT; ++n) {
        if (ngx_array_init(&cmcf->events[n], cf->pool, 1,
                sizeof(ngx_rtmp_handler_pt)) != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    if (ngx_array_init(&cmcf->amf, cf->pool, 1,
                sizeof(ngx_rtmp_amf_handler_t)) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_init_event_handlers(ngx_conf_t *cf, ngx_rtmp_core_main_conf_t *cmcf)
{
    ngx_hash_init_t             calls_hash;
    ngx_rtmp_handler_pt        *eh;
    ngx_rtmp_amf_handler_t     *h;
    ngx_hash_key_t             *ha;
    size_t                      n, m;

    static size_t               pm_events[] = {
        NGX_RTMP_MSG_CHUNK_SIZE,
        NGX_RTMP_MSG_ABORT,
        NGX_RTMP_MSG_ACK,
        NGX_RTMP_MSG_ACK_SIZE,
        NGX_RTMP_MSG_BANDWIDTH
    };

    static size_t               amf_events[] = {
        NGX_RTMP_MSG_AMF_CMD,
        NGX_RTMP_MSG_AMF_META,
        NGX_RTMP_MSG_AMF_SHARED,
        NGX_RTMP_MSG_AMF3_CMD,
        NGX_RTMP_MSG_AMF3_META,
        NGX_RTMP_MSG_AMF3_SHARED
    };

    /* init standard protocol events */
    /* 初始化当接收到 pm_events 数组中包含的 RTMP 消息时调用的回调函数
     * 支持的 RTMP 消息有 chunk_size、abort、ack、ack_size、bandwidth */
    for(n = 0; n < sizeof(pm_events) / sizeof(pm_events[0]); ++n) {
        eh = ngx_array_push(&cmcf->events[pm_events[n]]);
        *eh = ngx_rtmp_protocol_message_handler;
    }

    /* init amf events */
    /* 初始化当接收到 amf_events 数组中包含的 amf 类型事件时调用的回调函数 */
    for(n = 0; n < sizeof(amf_events) / sizeof(amf_events[0]); ++n) {
        eh = ngx_array_push(&cmcf->events[amf_events[n]]);
        *eh = ngx_rtmp_amf_message_handler;
    }

    /* init user protocol events */
    eh = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_USER]);
    *eh = ngx_rtmp_user_message_handler;

    /* aggregate to audio/video map */
    eh = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_AGGREGATE]);
    *eh = ngx_rtmp_aggregate_message_handler;

    /* init amf callbacks */
    ngx_array_init(&cmcf->amf_arrays, cf->pool, 1, sizeof(ngx_hash_key_t));

    /* cmcf->amf 数组是调用所有的 RTMP 模块的 postconfiguration 方法
     * 由各 RTMP 模块向该数组中添加自己想要处理 amf 回调方法，主要有
     * 三个 RTMP 模块会向该数组中添加，分别为 ngx_rtmp_cmd_module、
     * ngx_rtmp_codec_module、ngx_rtmp_relay_module */
    h = cmcf->amf.elts;
    for(n = 0; n < cmcf->amf.nelts; ++n, ++h) {
        ha = cmcf->amf_arrays.elts;
        for(m = 0; m < cmcf->amf_arrays.nelts; ++m, ++ha) {
            if (h->name.len == ha->key.len
                    && !ngx_strncmp(h->name.data, ha->key.data, ha->key.len))
            {
                break;
            }
        }
        /* 若 amf 数组中没有存在与 amf_arrays 中相同的 amf 字符串,
         * 则需要重新为该 h->name 进行散列 */
        if (m == cmcf->amf_arrays.nelts) {
            ha = ngx_array_push(&cmcf->amf_arrays);
            ha->key = h->name;
            /* 根据关键字散列出映射的槽 */
            ha->key_hash = ngx_hash_key_lc(ha->key.data, ha->key.len);
            ha->value = ngx_array_create(cf->pool, 1,
                    sizeof(ngx_rtmp_handler_pt));
            if (ha->value == NULL) {
                return NGX_ERROR;
            }
        }

        eh = ngx_array_push((ngx_array_t*)ha->value);
        *eh = h->handler;
    }

    /* 指向 amf_hash 散列表 */
    calls_hash.hash = &cmcf->amf_hash;
    calls_hash.key = ngx_hash_key_lc;
    calls_hash.max_size = 512;
    calls_hash.bucket_size = ngx_cacheline_size;
    calls_hash.name = "amf_hash";
    calls_hash.pool = cf->pool;
    calls_hash.temp_pool = NULL;
    // /* 初始化该散列表 */ 
    // 有个疑问，cmcf->amf_arrays.elts 存放的到底是什么 放的是 ngx_rtmp_handler_pt 的数组
    if (ngx_hash_init(&calls_hash, cmcf->amf_arrays.elts, cmcf->amf_arrays.nelts)
            != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_add_ports(ngx_conf_t *cf, ngx_array_t *ports,
    ngx_rtmp_listen_t *listen)
{
    in_port_t              p;
    ngx_uint_t             i;
    struct sockaddr       *sa;
    struct sockaddr_in    *sin;
    ngx_rtmp_conf_port_t  *port;
    ngx_rtmp_conf_addr_t  *addr;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6   *sin6;
#endif

    sa = (struct sockaddr *) &listen->sockaddr;

    switch (sa->sa_family) {

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) sa;
        p = sin6->sin6_port;
        break;
#endif

    default: /* AF_INET */
        sin = (struct sockaddr_in *) sa;
        p = sin->sin_port;
        break;
    }

    port = ports->elts;
    for (i = 0; i < ports->nelts; i++) {
        if (p == port[i].port && sa->sa_family == port[i].family) {

            /* a port is already in the port list */

            port = &port[i];
            goto found;
        }
    }

    /* add a port to the port list */

    port = ngx_array_push(ports);
    if (port == NULL) {
        return NGX_ERROR;
    }

    port->family = sa->sa_family;
    port->port = p;

    if (ngx_array_init(&port->addrs, cf->temp_pool, 2,
                       sizeof(ngx_rtmp_conf_addr_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

found:

    addr = ngx_array_push(&port->addrs);
    if (addr == NULL) {
        return NGX_ERROR;
    }

    addr->sockaddr = (struct sockaddr *) &listen->sockaddr;
    addr->socklen = listen->socklen;
    addr->ctx = listen->ctx;
    addr->bind = listen->bind;
    addr->wildcard = listen->wildcard;
    addr->so_keepalive = listen->so_keepalive;
    addr->proxy_protocol = listen->proxy_protocol;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    addr->tcp_keepidle = listen->tcp_keepidle;
    addr->tcp_keepintvl = listen->tcp_keepintvl;
    addr->tcp_keepcnt = listen->tcp_keepcnt;
#endif
#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    addr->ipv6only = listen->ipv6only;
#endif

    return NGX_OK;
}


static char *
ngx_rtmp_optimize_servers(ngx_conf_t *cf, ngx_array_t *ports)
{
    ngx_uint_t             i, p, last, bind_wildcard;
    ngx_listening_t       *ls;
    ngx_rtmp_port_t       *mport;
    ngx_rtmp_conf_port_t  *port;
    ngx_rtmp_conf_addr_t  *addr;

    port = ports->elts;
    for (p = 0; p < ports->nelts; p++) {

        ngx_sort(port[p].addrs.elts, (size_t) port[p].addrs.nelts,
                 sizeof(ngx_rtmp_conf_addr_t), ngx_rtmp_cmp_conf_addrs);

        addr = port[p].addrs.elts;
        last = port[p].addrs.nelts;

        /*
         * if there is the binding to the "*:port" then we need to bind()
         * to the "*:port" only and ignore the other bindings
         */

        if (addr[last - 1].wildcard) {
            addr[last - 1].bind = 1;
            bind_wildcard = 1;

        } else {
            bind_wildcard = 0;
        }

        i = 0;

        while (i < last) {

            if (bind_wildcard && !addr[i].bind) {
                i++;
                continue;
            }

            ls = ngx_create_listening(cf, addr[i].sockaddr, addr[i].socklen);
            if (ls == NULL) {
                return NGX_CONF_ERROR;
            }

            ls->addr_ntop = 1;
            ls->handler = ngx_rtmp_init_connection;
            ls->pool_size = 4096;

            /* TODO: error_log directive */
            ls->logp = &cf->cycle->new_log;
            ls->log.data = &ls->addr_text;
            ls->log.handler = ngx_accept_log_error;

            ls->keepalive = addr[i].so_keepalive;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
            ls->keepidle = addr[i].tcp_keepidle;
            ls->keepintvl = addr[i].tcp_keepintvl;
            ls->keepcnt = addr[i].tcp_keepcnt;
#endif

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
            ls->ipv6only = addr[i].ipv6only;
#endif

            mport = ngx_palloc(cf->pool, sizeof(ngx_rtmp_port_t));
            if (mport == NULL) {
                return NGX_CONF_ERROR;
            }

            ls->servers = mport;

            if (i == last - 1) {
                mport->naddrs = last;

            } else {
                mport->naddrs = 1;
                i = 0;
            }

            switch (ls->sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
            case AF_INET6:
                if (ngx_rtmp_add_addrs6(cf, mport, addr) != NGX_OK) {
                    return NGX_CONF_ERROR;
                }
                break;
#endif
            default: /* AF_INET */
                if (ngx_rtmp_add_addrs(cf, mport, addr) != NGX_OK) {
                    return NGX_CONF_ERROR;
                }
                break;
            }

            addr++;
            last--;
        }
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_add_addrs(ngx_conf_t *cf, ngx_rtmp_port_t *mport,
    ngx_rtmp_conf_addr_t *addr)
{
    u_char              *p;
    size_t               len;
    ngx_uint_t           i;
    ngx_rtmp_in_addr_t  *addrs;
    struct sockaddr_in  *sin;
    u_char               buf[NGX_SOCKADDR_STRLEN];

    mport->addrs = ngx_pcalloc(cf->pool,
                               mport->naddrs * sizeof(ngx_rtmp_in_addr_t));
    if (mport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs = mport->addrs;

    for (i = 0; i < mport->naddrs; i++) {

        sin = (struct sockaddr_in *) addr[i].sockaddr;
        addrs[i].addr = sin->sin_addr.s_addr;

        addrs[i].conf.ctx = addr[i].ctx;

        len = ngx_sock_ntop(addr[i].sockaddr,
#if (nginx_version >= 1005003)
                            addr[i].socklen,
#endif
                            buf, NGX_SOCKADDR_STRLEN, 1);

        p = ngx_pnalloc(cf->pool, len);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(p, buf, len);

        addrs[i].conf.addr_text.len = len;
        addrs[i].conf.addr_text.data = p;
        addrs[i].conf.proxy_protocol = addr->proxy_protocol;
    }

    return NGX_OK;
}


#if (NGX_HAVE_INET6)

static ngx_int_t
ngx_rtmp_add_addrs6(ngx_conf_t *cf, ngx_rtmp_port_t *mport,
    ngx_rtmp_conf_addr_t *addr)
{
    u_char               *p;
    size_t                len;
    ngx_uint_t            i;
    ngx_rtmp_in6_addr_t  *addrs6;
    struct sockaddr_in6  *sin6;
    u_char                buf[NGX_SOCKADDR_STRLEN];

    mport->addrs = ngx_pcalloc(cf->pool,
                               mport->naddrs * sizeof(ngx_rtmp_in6_addr_t));
    if (mport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs6 = mport->addrs;

    for (i = 0; i < mport->naddrs; i++) {

        sin6 = (struct sockaddr_in6 *) addr[i].sockaddr;
        addrs6[i].addr6 = sin6->sin6_addr;

        addrs6[i].conf.ctx = addr[i].ctx;

        len = ngx_sock_ntop(addr[i].sockaddr,
#if (nginx_version >= 1005003)
                            addr[i].socklen,
#endif
                            buf, NGX_SOCKADDR_STRLEN, 1);

        p = ngx_pnalloc(cf->pool, len);
        if (p == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(p, buf, len);

        addrs6[i].conf.addr_text.len = len;
        addrs6[i].conf.addr_text.data = p;
        addrs6[i].conf.proxy_protocol = addr->proxy_protocol;
    }

    return NGX_OK;
}

#endif


static ngx_int_t
ngx_rtmp_cmp_conf_addrs(const void *one, const void *two)
{
    ngx_rtmp_conf_addr_t  *first, *second;

    first = (ngx_rtmp_conf_addr_t *) one;
    second = (ngx_rtmp_conf_addr_t *) two;

    if (first->wildcard) {
        /* a wildcard must be the last resort, shift it to the end */
        return 1;
    }

    if (first->bind && !second->bind) {
        /* shift explicit bind()ed addresses to the start */
        return -1;
    }

    if (!first->bind && second->bind) {
        /* shift explicit bind()ed addresses to the start */
        return 1;
    }

    /* do not sort by default */

    return 0;
}


ngx_int_t
ngx_rtmp_fire_event(ngx_rtmp_session_t *s, ngx_uint_t evt,
        ngx_rtmp_header_t *h, ngx_chain_t *in)
{
    ngx_rtmp_core_main_conf_t      *cmcf;
    ngx_array_t                    *ch;
    ngx_rtmp_handler_pt            *hh;
    size_t                          n;

    cmcf = ngx_rtmp_get_module_main_conf(s, ngx_rtmp_core_module);

    ch = &cmcf->events[evt];
    hh = ch->elts;
    for(n = 0; n < ch->nelts; ++n, ++hh) {
        if (*hh && (*hh)(s, h, in) != NGX_OK) {
            return NGX_ERROR;
        }
    }
    return NGX_OK;
}


void *
ngx_rtmp_rmemcpy(void *dst, const void* src, size_t n)
{
    u_char     *d, *s;

    d = dst;
    s = (u_char*)src + n - 1;

    while(s >= (u_char*)src) {
        *d++ = *s--;
    }

    return dst;
}


static ngx_int_t
ngx_rtmp_init_process(ngx_cycle_t *cycle)
{
#if (nginx_version >= 1007005)
    ngx_queue_init(&ngx_rtmp_init_queue);
#endif
    return NGX_OK;
}
