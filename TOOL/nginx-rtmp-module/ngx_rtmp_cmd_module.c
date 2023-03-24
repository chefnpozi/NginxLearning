
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_streams.h"


#define NGX_RTMP_FMS_VERSION        "FMS/3,0,1,123"
#define NGX_RTMP_CAPABILITIES       31


static ngx_int_t ngx_rtmp_cmd_connect(ngx_rtmp_session_t *s,
       ngx_rtmp_connect_t *v);
static ngx_int_t ngx_rtmp_cmd_disconnect(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_cmd_create_stream(ngx_rtmp_session_t *s,
       ngx_rtmp_create_stream_t *v);
static ngx_int_t ngx_rtmp_cmd_close_stream(ngx_rtmp_session_t *s,
       ngx_rtmp_close_stream_t *v);
static ngx_int_t ngx_rtmp_cmd_delete_stream(ngx_rtmp_session_t *s,
       ngx_rtmp_delete_stream_t *v);
static ngx_int_t ngx_rtmp_cmd_publish(ngx_rtmp_session_t *s,
       ngx_rtmp_publish_t *v);
static ngx_int_t ngx_rtmp_cmd_play(ngx_rtmp_session_t *s,
       ngx_rtmp_play_t *v);
static ngx_int_t ngx_rtmp_cmd_seek(ngx_rtmp_session_t *s,
       ngx_rtmp_seek_t *v);
static ngx_int_t ngx_rtmp_cmd_pause(ngx_rtmp_session_t *s,
       ngx_rtmp_pause_t *v);


static ngx_int_t ngx_rtmp_cmd_stream_begin(ngx_rtmp_session_t *s,
       ngx_rtmp_stream_begin_t *v);
static ngx_int_t ngx_rtmp_cmd_stream_eof(ngx_rtmp_session_t *s,
       ngx_rtmp_stream_eof_t *v);
static ngx_int_t ngx_rtmp_cmd_stream_dry(ngx_rtmp_session_t *s,
       ngx_rtmp_stream_dry_t *v);
static ngx_int_t ngx_rtmp_cmd_recorded(ngx_rtmp_session_t *s,
       ngx_rtmp_recorded_t *v);
static ngx_int_t ngx_rtmp_cmd_set_buflen(ngx_rtmp_session_t *s,
       ngx_rtmp_set_buflen_t *v);


ngx_rtmp_connect_pt         ngx_rtmp_connect;
ngx_rtmp_disconnect_pt      ngx_rtmp_disconnect;
ngx_rtmp_create_stream_pt   ngx_rtmp_create_stream;
ngx_rtmp_close_stream_pt    ngx_rtmp_close_stream;
ngx_rtmp_delete_stream_pt   ngx_rtmp_delete_stream;
ngx_rtmp_publish_pt         ngx_rtmp_publish;
ngx_rtmp_play_pt            ngx_rtmp_play;
ngx_rtmp_seek_pt            ngx_rtmp_seek;
ngx_rtmp_pause_pt           ngx_rtmp_pause;


ngx_rtmp_stream_begin_pt    ngx_rtmp_stream_begin;
ngx_rtmp_stream_eof_pt      ngx_rtmp_stream_eof;
ngx_rtmp_stream_dry_pt      ngx_rtmp_stream_dry;
ngx_rtmp_recorded_pt        ngx_rtmp_recorded;
ngx_rtmp_set_buflen_pt      ngx_rtmp_set_buflen;


static ngx_int_t ngx_rtmp_cmd_postconfiguration(ngx_conf_t *cf);


static ngx_rtmp_module_t  ngx_rtmp_cmd_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_rtmp_cmd_postconfiguration,         /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    NULL,                                   /* create app configuration */
    NULL                                    /* merge app configuration */
};


ngx_module_t  ngx_rtmp_cmd_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_cmd_module_ctx,               /* module context */
    NULL,                                   /* module directives */
    NGX_RTMP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


void
ngx_rtmp_cmd_fill_args(u_char name[NGX_RTMP_MAX_NAME],
        u_char args[NGX_RTMP_MAX_ARGS])
{
    u_char      *p;

    p = (u_char *)ngx_strchr(name, '?');
    if (p == NULL) {
        return;
    }

    *p++ = 0;
    ngx_cpystrn(args, p, NGX_RTMP_MAX_ARGS);
}


static ngx_int_t
ngx_rtmp_cmd_connect_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    size_t                      len;

    static ngx_rtmp_connect_t   v;

    static ngx_rtmp_amf_elt_t  in_cmd[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("app"),
          v.app, sizeof(v.app) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("flashVer"),
          v.flashver, sizeof(v.flashver) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("swfUrl"),
          v.swf_url, sizeof(v.swf_url) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("tcUrl"),
          v.tc_url, sizeof(v.tc_url) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("audioCodecs"),
          &v.acodecs, sizeof(v.acodecs) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("videoCodecs"),
          &v.vcodecs, sizeof(v.vcodecs) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("pageUrl"),
          v.page_url, sizeof(v.page_url) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("objectEncoding"),
          &v.object_encoding, 0},
    };

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, 0 },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          in_cmd, sizeof(in_cmd) },
    };

    ngx_memzero(&v, sizeof(v));
    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    len = ngx_strlen(v.app);
    if (len > 10 && !ngx_memcmp(v.app + len - 10, "/_definst_", 10)) {
        v.app[len - 10] = 0;
    } else if (len && v.app[len - 1] == '/') {
        v.app[len - 1] = 0;
    }

    ngx_rtmp_cmd_fill_args(v.app, v.args);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "connect: app='%s' args='%s' flashver='%s' swf_url='%s' "
            "tc_url='%s' page_url='%s' acodecs=%uD vcodecs=%uD "
            "object_encoding=%ui",
            v.app, v.args, v.flashver, v.swf_url, v.tc_url, v.page_url,
            (uint32_t)v.acodecs, (uint32_t)v.vcodecs,
            (ngx_int_t)v.object_encoding);

    return ngx_rtmp_connect(s, &v);   // ngx_rtmp_notify_connect
}


static ngx_int_t
ngx_rtmp_cmd_connect(ngx_rtmp_session_t *s, ngx_rtmp_connect_t *v)
{
    ngx_rtmp_core_srv_conf_t   *cscf;
    ngx_rtmp_core_app_conf_t  **cacfp;
    ngx_uint_t                  n;
    ngx_rtmp_header_t           h;
    u_char                     *p;

    static double               trans;
    static double               capabilities = NGX_RTMP_CAPABILITIES;
    static double               object_encoding = 0;

    /* 以下内容为服务器将要对客户端的 connect 命令返回的 amf 类型的响应 */
    static ngx_rtmp_amf_elt_t  out_obj[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("fmsVer"),
          NGX_RTMP_FMS_VERSION, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("capabilities"),
          &capabilities, 0 },
    };

    static ngx_rtmp_amf_elt_t  out_inf[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("level"),
          "status", 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_string("code"),
          "NetConnection.Connect.Success", 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_string("description"),
          "Connection succeeded.", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("objectEncoding"),
          &object_encoding, 0 }
    };

    static ngx_rtmp_amf_elt_t  out_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          "_result", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &trans, 0 },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          out_obj, sizeof(out_obj) },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          out_inf, sizeof(out_inf) },
    };

    if (s->connected) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "connect: duplicate connection");
        return NGX_ERROR;
    }

    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    trans = v->trans;

    /* fill session parameters */
    s->connected = 1;

    ngx_memzero(&h, sizeof(h));
    h.csid = NGX_RTMP_CSID_AMF_INI;
    h.type = NGX_RTMP_MSG_AMF_CMD;


#define NGX_RTMP_SET_STRPAR(name)                                             \
    s->name.len = ngx_strlen(v->name);                                        \
    s->name.data = ngx_palloc(s->connection->pool, s->name.len);              \
    ngx_memcpy(s->name.data, v->name, s->name.len)

    NGX_RTMP_SET_STRPAR(app);
    NGX_RTMP_SET_STRPAR(args);
    NGX_RTMP_SET_STRPAR(flashver);
    NGX_RTMP_SET_STRPAR(swf_url);
    NGX_RTMP_SET_STRPAR(tc_url);
    NGX_RTMP_SET_STRPAR(page_url);

#undef NGX_RTMP_SET_STRPAR

    p = ngx_strlchr(s->app.data, s->app.data + s->app.len, '?');
    if (p) {
        s->app.len = (p - s->app.data);
    }

    s->acodecs = (uint32_t) v->acodecs;
    s->vcodecs = (uint32_t) v->vcodecs;

    // 找到客户端 connect 的应用配置
    /* find application & set app_conf */
    cacfp = cscf->applications.elts;
    for(n = 0; n < cscf->applications.nelts; ++n, ++cacfp) {
        if ((*cacfp)->name.len == s->app.len &&
            ngx_strncmp((*cacfp)->name.data, s->app.data, s->app.len) == 0)
        {
            /* found app! */
            
            ngx_log_error(NGX_LOG_NOTICE, s->connection->log, 0,
                "found app is '%V'", &s->app);
            s->app_conf = (*cacfp)->app_conf;
            break;
        }
    }

    if (s->app_conf == NULL) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "connect: application not found: '%V'", &s->app);
        return NGX_ERROR;
    }

    object_encoding = v->object_encoding;

            /* 发送应答窗口大小：ack_size 给客户端，该消息是用来通知对方应答窗口的大小，
            * 发送方在发送了等于窗口大小的数据之后，等的爱接收对方的应答消息（在接收  
            * 到应答消息之前停止发送数据）。接收当必须发送应答消息，在会话开始时，在  
            * 会话开始时，会从上一次发送应答之后接收到了等于窗口大小的数据 */
    return ngx_rtmp_send_ack_size(s, cscf->ack_window) != NGX_OK ||
            /* 发送 设置流带宽消息。发送此消息来说明对方的出口带宽限制，接收方以此来限制  
            * 自己的出口带宽，即限制未被应答的消息数据大小。接收到此消息的一方，如果  
            * 窗口大小与上一次发送的不一致，应该回复应答窗口大小的消息 */
           ngx_rtmp_send_bandwidth(s, cscf->ack_window,
                                   NGX_RTMP_LIMIT_DYNAMIC) != NGX_OK ||
            /* 发送 设置块消息消息，用来通知对方新的最大的块大小。 */
           ngx_rtmp_send_chunk_size(s, cscf->chunk_size) != NGX_OK ||
           ngx_rtmp_send_amf(s, &h, out_elts,
                             sizeof(out_elts) / sizeof(out_elts[0]))
           != NGX_OK ? NGX_ERROR : NGX_OK;
           // 服务端相应客户端connect命令消息后，客户端会发送releaseStream命令消息给服务器，nginx-rtmp-module对该命令不进行处理
           // 接着等待接收下一条命令消息
           // 接着服务端收到来自客户端的createStream命令消息，会调用 ngx_rtmp_cmd_create_stream_init
}


static ngx_int_t
ngx_rtmp_cmd_create_stream_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                                ngx_chain_t *in)
{
    // 服务器接收到客户端发来的 createStream 命令消息
    static ngx_rtmp_create_stream_t     v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, sizeof(v.trans) },
    };

    // 解析该 createStream 命令消息，获取 v.trans 值
    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "createStream");

    // 接着，从该函数中开始调用 ngx_rtmp_create_stream 构建的函数链表。这里调用到的是 ngx_rtmp_cmd_create_stream 函数
    return ngx_rtmp_create_stream(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_create_stream(ngx_rtmp_session_t *s, ngx_rtmp_create_stream_t *v)
{
    // 该函数主要是 构建 发送对于 createStream 的响应
    /* support one message stream per connection */
    static double               stream;
    static double               trans;
    ngx_rtmp_header_t           h;

    static ngx_rtmp_amf_elt_t  out_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          "_result", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &trans, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &stream, sizeof(stream) },
    };

    trans = v->trans;
    stream = NGX_RTMP_MSID;

    ngx_memzero(&h, sizeof(h));

    h.csid = NGX_RTMP_CSID_AMF_INI;
    h.type = NGX_RTMP_MSG_AMF_CMD;
    // 发送对于 createStream 的响应
    // 客户端会接收到服务器对 createStream 的响应包：_result() 会调用ngx_rtmp_relay_on_result
    return ngx_rtmp_send_amf(s, &h, out_elts,
                             sizeof(out_elts) / sizeof(out_elts[0])) == NGX_OK ?
           NGX_DONE : NGX_ERROR;
           /* 接着，客户端收到对于createStream的响应后，会发送 publish 给服务器，
           publish 用于发布一条带有名字的流到服务器，其他的客户端可以使用此流名来播放该流，
           接收其发布的音频 视频 以及其他的数据信息 从抓包截图中可知，publish type 为 'live'，即服务器不会保存客户端发布的流到文件中
           对于publish命令消息，会调用 ngx_rtmp_cmd_publish_init 
           */
}


static ngx_int_t
ngx_rtmp_cmd_close_stream_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                               ngx_chain_t *in)
{
    static ngx_rtmp_close_stream_t     v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.stream, 0 },
    };

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "closeStream");

    return ngx_rtmp_close_stream(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_delete_stream_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                                ngx_chain_t *in)
{
    static ngx_rtmp_delete_stream_t     v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.stream, 0 },
    };

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    return ngx_rtmp_delete_stream(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_delete_stream(ngx_rtmp_session_t *s, ngx_rtmp_delete_stream_t *v)
{
    ngx_rtmp_close_stream_t         cv;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "deleteStream");

    cv.stream = 0;

    return ngx_rtmp_close_stream(s, &cv);
}


static ngx_int_t
ngx_rtmp_cmd_publish_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{   
    // 接收到 publish 信令后开始执行
    static ngx_rtmp_publish_t       v;

    static ngx_rtmp_amf_elt_t      in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.name, sizeof(v.name) },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.type, sizeof(v.type) },
    };

    ngx_memzero(&v, sizeof(v));

    /* 从 publish 命令消息中获取 in_elts 中指定的值 */
    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_rtmp_cmd_fill_args(v.name, v.args);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "publish: name='%s' args='%s' type=%s silent=%d",
                  v.name, v.args, v.type, v.silent);

    /* 接着，该函数开始调用 ngx_rtmp_publish 构建的函数链表。
    从 nginx-rtmp 的源码和 nginx.conf 的配置可知，主要调用
    ngx_rtmp_relay_publish 和 ngx_rtmp_live_publish 两个函数。
    由 rtmp 模块的排序，首先调用 ngx_rtmp_relay_publish */
    return ngx_rtmp_publish(s, &v);
    /* 当前客户端连接的 application 为 live，而该 application{} 下没有 push，
    因此这里主要调用 ngx_rtmp_live_publish */
}


static ngx_int_t
ngx_rtmp_cmd_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_cmd_play_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_play_t          v;

    static ngx_rtmp_amf_elt_t       in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.name, sizeof(v.name) },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.start, 0 },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.duration, 0 },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_BOOLEAN,
          ngx_null_string,
          &v.reset, 0 }
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_rtmp_cmd_fill_args(v.name, v.args);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "play: name='%s' args='%s' start=%i duration=%i "
                  "reset=%i silent=%i",
                  v.name, v.args, (ngx_int_t) v.start,
                  (ngx_int_t) v.duration, (ngx_int_t) v.reset,
                  (ngx_int_t) v.silent);

    return ngx_rtmp_play(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_play2_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_play_t          v;
    static ngx_rtmp_close_stream_t  vc;

    static ngx_rtmp_amf_elt_t       in_obj[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("start"),
          &v.start, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_string("streamName"),
          &v.name, sizeof(v.name) },
    };

    static ngx_rtmp_amf_elt_t       in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          &in_obj, sizeof(in_obj) }
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_rtmp_cmd_fill_args(v.name, v.args);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "play2: name='%s' args='%s' start=%i",
                  v.name, v.args, (ngx_int_t) v.start);

    /* continue from current timestamp */

    if (v.start < 0) {
        v.start = s->current_time;
    }

    ngx_memzero(&vc, sizeof(vc));

    /* close_stream should be synchronous */
    ngx_rtmp_close_stream(s, &vc);

    return ngx_rtmp_play(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_pause_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_pause_t     v;

    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_BOOLEAN,
          ngx_null_string,
          &v.pause, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.position, 0 },
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "pause: pause=%i position=%i",
                    (ngx_int_t) v.pause, (ngx_int_t) v.position);

    return ngx_rtmp_pause(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_pause(ngx_rtmp_session_t *s, ngx_rtmp_pause_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_disconnect_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                        ngx_chain_t *in)
{
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "disconnect");

    return ngx_rtmp_disconnect(s);
}


static ngx_int_t
ngx_rtmp_cmd_disconnect(ngx_rtmp_session_t *s)
{
    return ngx_rtmp_delete_stream(s, NULL);
}


static ngx_int_t
ngx_rtmp_cmd_seek_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_seek_t         v;

    static ngx_rtmp_amf_elt_t      in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.offset, sizeof(v.offset) },
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "seek: offset=%i", (ngx_int_t) v.offset);

    return ngx_rtmp_seek(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_seek(ngx_rtmp_session_t *s, ngx_rtmp_seek_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_stream_dry(ngx_rtmp_session_t *s, ngx_rtmp_stream_dry_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_recorded(ngx_rtmp_session_t *s,
                      ngx_rtmp_recorded_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_set_buflen(ngx_rtmp_session_t *s, ngx_rtmp_set_buflen_t *v)
{
    return NGX_OK;
}


static ngx_rtmp_amf_handler_t ngx_rtmp_cmd_map[] = { // 挂载ph
    { ngx_string("connect"),            ngx_rtmp_cmd_connect_init           },
    { ngx_string("createStream"),       ngx_rtmp_cmd_create_stream_init     },
    { ngx_string("closeStream"),        ngx_rtmp_cmd_close_stream_init      },
    { ngx_string("deleteStream"),       ngx_rtmp_cmd_delete_stream_init     },
    { ngx_string("publish"),            ngx_rtmp_cmd_publish_init           },
    { ngx_string("play"),               ngx_rtmp_cmd_play_init              },
    { ngx_string("play2"),              ngx_rtmp_cmd_play2_init             },
    { ngx_string("seek"),               ngx_rtmp_cmd_seek_init              },
    { ngx_string("pause"),              ngx_rtmp_cmd_pause_init             },
    { ngx_string("pauseraw"),           ngx_rtmp_cmd_pause_init             },
};


static ngx_int_t
ngx_rtmp_cmd_postconfiguration(ngx_conf_t *cf)
{
    ngx_rtmp_core_main_conf_t          *cmcf;
    ngx_rtmp_handler_pt                *h;
    ngx_rtmp_amf_handler_t             *ch, *bh;
    size_t                              n, ncalls;

    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);

    /* redirect disconnects to deleteStream
     * to free client modules from registering
     * disconnect callback */

    h = ngx_array_push(&cmcf->events[NGX_RTMP_DISCONNECT]);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_rtmp_cmd_disconnect_init;

    /* register AMF callbacks */

    ncalls = sizeof(ngx_rtmp_cmd_map) / sizeof(ngx_rtmp_cmd_map[0]);

    ch = ngx_array_push_n(&cmcf->amf, ncalls);  // 准备挂载 ngx_rtmp_cmd_map 中的各个 init 函数
    if (ch == NULL) {
        return NGX_ERROR;
    }

    bh = ngx_rtmp_cmd_map;

    for(n = 0; n < ncalls; ++n, ++ch, ++bh) {
        *ch = *bh;
    }

    ngx_rtmp_connect = ngx_rtmp_cmd_connect;
    ngx_rtmp_disconnect = ngx_rtmp_cmd_disconnect;
    ngx_rtmp_create_stream = ngx_rtmp_cmd_create_stream;
    ngx_rtmp_close_stream = ngx_rtmp_cmd_close_stream;
    ngx_rtmp_delete_stream = ngx_rtmp_cmd_delete_stream;
    ngx_rtmp_publish = ngx_rtmp_cmd_publish;
    ngx_rtmp_play = ngx_rtmp_cmd_play;
    ngx_rtmp_seek = ngx_rtmp_cmd_seek;
    ngx_rtmp_pause = ngx_rtmp_cmd_pause;

    ngx_rtmp_stream_begin = ngx_rtmp_cmd_stream_begin;
    ngx_rtmp_stream_eof = ngx_rtmp_cmd_stream_eof;
    ngx_rtmp_stream_dry = ngx_rtmp_cmd_stream_dry;
    ngx_rtmp_recorded = ngx_rtmp_cmd_recorded;
    ngx_rtmp_set_buflen = ngx_rtmp_cmd_set_buflen;

    return NGX_OK;
}
