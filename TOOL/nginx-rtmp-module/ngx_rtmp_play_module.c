
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>
#include "ngx_rtmp_play_module.h"
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_netcall_module.h"
#include "ngx_rtmp_streams.h"


static ngx_rtmp_play_pt                 next_play;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_seek_pt                 next_seek;
static ngx_rtmp_pause_pt                next_pause;


static char *ngx_rtmp_play_url(ngx_conf_t *cf, ngx_command_t *cmd,
       void *conf);
static void *ngx_rtmp_play_create_main_conf(ngx_conf_t *cf);
static ngx_int_t ngx_rtmp_play_postconfiguration(ngx_conf_t *cf);
static void * ngx_rtmp_play_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_play_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);

static ngx_int_t ngx_rtmp_play_do_init(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_play_do_done(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_play_do_start(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_play_do_stop(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_play_do_seek(ngx_rtmp_session_t *s,
                                       ngx_uint_t timestamp);

static ngx_int_t ngx_rtmp_play_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v);
static ngx_int_t ngx_rtmp_play_seek(ngx_rtmp_session_t *s, ngx_rtmp_seek_t *v);
static ngx_int_t ngx_rtmp_play_pause(ngx_rtmp_session_t *s,
                                     ngx_rtmp_pause_t *v);
static void ngx_rtmp_play_send(ngx_event_t *e);
static ngx_int_t ngx_rtmp_play_open(ngx_rtmp_session_t *s, double start);
static ngx_int_t ngx_rtmp_play_remote_handle(ngx_rtmp_session_t *s,
       void *arg, ngx_chain_t *in);
static ngx_chain_t * ngx_rtmp_play_remote_create(ngx_rtmp_session_t *s,
       void *arg, ngx_pool_t *pool);
static ngx_int_t ngx_rtmp_play_open_remote(ngx_rtmp_session_t *s,
       ngx_rtmp_play_t *v);
static ngx_int_t ngx_rtmp_play_next_entry(ngx_rtmp_session_t *s,
       ngx_rtmp_play_t *v);
static ngx_rtmp_play_entry_t * ngx_rtmp_play_get_current_entry(
       ngx_rtmp_session_t *s);
static void ngx_rtmp_play_cleanup_local_file(ngx_rtmp_session_t *s);
static void ngx_rtmp_play_copy_local_file(ngx_rtmp_session_t *s, u_char *name);
static u_char * ngx_rtmp_play_get_local_file_path(ngx_rtmp_session_t *s);


static ngx_command_t  ngx_rtmp_play_commands[] = {

    { ngx_string("play"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_1MORE,
      ngx_rtmp_play_url,
      NGX_RTMP_APP_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("play_temp_path"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_play_app_conf_t, temp_path),
      NULL },

    { ngx_string("play_local_path"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_play_app_conf_t, local_path),
      NULL },

      ngx_null_command
};


static ngx_rtmp_module_t  ngx_rtmp_play_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_rtmp_play_postconfiguration,        /* postconfiguration */
    ngx_rtmp_play_create_main_conf,         /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_rtmp_play_create_app_conf,          /* create app configuration */
    ngx_rtmp_play_merge_app_conf            /* merge app configuration */
};


ngx_module_t  ngx_rtmp_play_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_play_module_ctx,              /* module context */
    ngx_rtmp_play_commands,                 /* module directives */
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


#define NGX_RTMP_PLAY_TMP_FILE              "nginx-rtmp-vod."


static void *
ngx_rtmp_play_create_main_conf(ngx_conf_t *cf)
{
    ngx_rtmp_play_main_conf_t      *pmcf;

    pmcf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_play_main_conf_t));
    if (pmcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&pmcf->fmts, cf->pool, 1,
                       sizeof(ngx_rtmp_play_fmt_t *))
        != NGX_OK)
    {
        return NULL;
    }

    return pmcf;
}


static void *
ngx_rtmp_play_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_play_app_conf_t      *pacf;

    pacf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_play_app_conf_t));
    if (pacf == NULL) {
        return NULL;
    }

    pacf->nbuckets = 1024;

    return pacf;
}


static char *
ngx_rtmp_play_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_play_app_conf_t *prev = parent;
    ngx_rtmp_play_app_conf_t *conf = child;
    ngx_rtmp_play_entry_t   **ppe;

    ngx_conf_merge_str_value(conf->temp_path, prev->temp_path, "/tmp");
    ngx_conf_merge_str_value(conf->local_path, prev->local_path, "");

    if (prev->entries.nelts == 0) {
        goto done;
    }

    if (conf->entries.nelts == 0) {
        conf->entries = prev->entries;
        goto done;
    }

    ppe = ngx_array_push_n(&conf->entries, prev->entries.nelts);
    if (ppe == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(ppe, prev->entries.elts, prev->entries.nelts * sizeof(void *));

done:

    if (conf->entries.nelts == 0) {
        return NGX_CONF_OK;
    }

    conf->ctx = ngx_pcalloc(cf->pool, sizeof(void *) * conf->nbuckets);
    if (conf->ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


/*该函数主要根据 ctx->name 通过 hash 的 key 方法，得到映射槽的位置 h，即 pacf->ctx[h]，然后将其插入到
  ngx_rtmp_play_module 的上下文结构体 ctx 的 ctx->next 构建的链表中.*/
static ngx_int_t
ngx_rtmp_play_join(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_ctx_t        *ctx, **pctx;
    ngx_rtmp_play_app_conf_t   *pacf;
    ngx_uint_t                  h;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: join");

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);
    if (ctx == NULL || ctx->joined) {
        return NGX_ERROR;
    }

    h = ngx_hash_key(ctx->name, ngx_strlen(ctx->name));
    pctx = &pacf->ctx[h % pacf->nbuckets];

    while (*pctx) {
        if (!ngx_strncmp((*pctx)->name, ctx->name, NGX_RTMP_MAX_NAME)) {
            break;
        }
        pctx = &(*pctx)->next;
    }

    ctx->next = *pctx;
    *pctx = ctx;
    ctx->joined = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_play_leave(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_ctx_t        *ctx, **pctx;
    ngx_rtmp_play_app_conf_t   *pacf;
    ngx_uint_t                  h;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: leave");

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);
    if (ctx == NULL || !ctx->joined) {
        return NGX_ERROR;
    }

    h = ngx_hash_key(ctx->name, ngx_strlen(ctx->name));
    pctx = &pacf->ctx[h % pacf->nbuckets];

    while (*pctx && *pctx != ctx) {
        pctx = &(*pctx)->next;
    }

    if (*pctx == NULL) {
        return NGX_ERROR;
    }

    *pctx = (*pctx)->next;
    ctx->joined = 0;

    return NGX_OK;
}


/*点播的最后一个调用就是在该函数中循环的，直到全部发送文件完成后，才返回*/
static void
ngx_rtmp_play_send(ngx_event_t *e)
{
    ngx_rtmp_session_t     *s = e->data;
    ngx_rtmp_play_ctx_t    *ctx;
    ngx_int_t               rc;
    ngx_uint_t              ts;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx == NULL || ctx->fmt == NULL || ctx->fmt->send == NULL) {
        return;
    }

    ts = 0;

    /* 调用相应封装的 send 函数 */
    rc = ctx->fmt->send(s, &ctx->file, &ts);

    if (rc > 0) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "play: send schedule %i", rc);

        ngx_add_timer(e, rc);
        return;
    }

    if (rc == NGX_AGAIN) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "play: send buffer full");

        ngx_post_event(e, &s->posted_dry_events);
        return;
    }

    if (rc == NGX_OK) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "play: send restart");

        /* 每发送完成一次，就将其放入到 ngx_posted_events 延迟队列中,
         * 然后由下一次 ngx_process_events_and_timers 函数中的循环
         * 调用到它 */
        ngx_post_event(e, &ngx_posted_events);
        return;
    }


    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: send done");

    /* 发送 stream_eof  */
    ngx_rtmp_send_stream_eof(s, NGX_RTMP_MSID);

    ngx_rtmp_send_play_status(s, "NetStream.Play.Complete", "status", ts, 0);

    ngx_rtmp_send_status(s, "NetStream.Play.Stop", "status", "Stopped");
}


static ngx_int_t
ngx_rtmp_play_do_init(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_ctx_t            *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx == NULL) {
        return NGX_ERROR;
    }

    /* 根据 fmt 调用相应封装格式的 init 函数，这里只支持两种封装格式，flv 和 mp4 */
    if (ctx->fmt && ctx->fmt->init &&
        ctx->fmt->init(s, &ctx->file, ctx->aindex, ctx->vindex) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_play_do_done(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_ctx_t            *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx == NULL) {
        return NGX_ERROR;
    }

    if (ctx->fmt && ctx->fmt->done &&
        ctx->fmt->done(s, &ctx->file) != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_play_do_start(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_ctx_t            *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: start");

    /* 调用相应封装的 start 函数 */
    if (ctx->fmt && ctx->fmt->start &&
        ctx->fmt->start(s, &ctx->file) != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* 将 ctx->send_evt 事件放入到 ngx_posted_events 延迟队列中 */
    ngx_post_event((&ctx->send_evt), &ngx_posted_events);

    /* 置位 playing 标志位，表示开始发送文件 */
    ctx->playing = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_play_do_seek(ngx_rtmp_session_t *s, ngx_uint_t timestamp)
{
    ngx_rtmp_play_ctx_t            *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: seek timestamp=%ui", timestamp);

    /* 调用相应封装的 seek 函数 */
    if (ctx->fmt && ctx->fmt->seek &&
        ctx->fmt->seek(s, &ctx->file, timestamp) != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* 若播放标志位已经置位，则将 ctx->send_evt 事件放入到 延迟队列 ngx_posted_events 中，
     * 正常来说应该还没有置位，仅在 ngx_rtmp_play_do_stark 中将其置 1 */
    if (ctx->playing) {
        ngx_post_event((&ctx->send_evt), &ngx_posted_events);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_play_do_stop(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_ctx_t            *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: stop");

    if (ctx->send_evt.timer_set) {
        ngx_del_timer(&ctx->send_evt);
    }

#if (nginx_version >= 1007005)
    if (ctx->send_evt.posted)
#else
    if (ctx->send_evt.prev)
#endif
    {
        ngx_delete_posted_event((&ctx->send_evt));
    }

    if (ctx->fmt && ctx->fmt->stop &&
        ctx->fmt->stop(s, &ctx->file) != NGX_OK)
    {
        return NGX_ERROR;
    }

    ctx->playing = 0;

    return NGX_OK;
}


/* This function returns pointer to a static buffer */

static u_char *
ngx_rtmp_play_get_local_file_path(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_app_conf_t       *pacf;
    ngx_rtmp_play_ctx_t            *ctx;
    u_char                         *p;
    static u_char                   path[NGX_MAX_PATH + 1];

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    p = ngx_snprintf(path, NGX_MAX_PATH, "%V/" NGX_RTMP_PLAY_TMP_FILE "%ui",
                     &pacf->temp_path, ctx->file_id);
    *p = 0;

    return path;
}


static void
ngx_rtmp_play_copy_local_file(ngx_rtmp_session_t *s, u_char *name)
{
    ngx_rtmp_play_app_conf_t   *pacf;
    ngx_rtmp_play_ctx_t        *ctx;
    u_char                     *path, *p;
    static u_char               dpath[NGX_MAX_PATH + 1];

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);
    if (pacf == NULL) {
        return;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);
    if (ctx == NULL || ctx->file_id == 0) {
        return;
    }

    path = ngx_rtmp_play_get_local_file_path(s);

    p = ngx_snprintf(dpath, NGX_MAX_PATH, "%V/%s%V", &pacf->local_path,
                     name + ctx->pfx_size, &ctx->sfx);
    *p = 0;

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: copy local file '%s' to '%s'", path, dpath);

    if (ngx_rename_file(path, dpath) == 0) {
        ctx->file_id = 0;
        return;
    }

    ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                  "play: error copying local file '%s' to '%s'",
                  path, dpath);

    ngx_rtmp_play_cleanup_local_file(s);
}


static void
ngx_rtmp_play_cleanup_local_file(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_ctx_t        *ctx;
    u_char                     *path;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);
    if (ctx == NULL || ctx->file_id == 0) {
        return;
    }

    path = ngx_rtmp_play_get_local_file_path(s);

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: deleting local file '%s'", path);

    ctx->file_id = 0;

    ngx_delete_file(path);
}


static ngx_int_t
ngx_rtmp_play_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{
    ngx_rtmp_play_ctx_t        *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);
    if (ctx == NULL) {
        goto next;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: close_stream");

    ngx_rtmp_play_do_stop(s);

    ngx_rtmp_play_do_done(s);

    if (ctx->file.fd != NGX_INVALID_FILE) {
        ngx_close_file(ctx->file.fd);
        ctx->file.fd = NGX_INVALID_FILE;

        ngx_rtmp_send_stream_eof(s, NGX_RTMP_MSID);

        ngx_rtmp_send_status(s, "NetStream.Play.Stop", "status",
                             "Stop video on demand");
    }

    if (ctx->file_id) {
        ngx_rtmp_play_cleanup_local_file(s);
    }

    ngx_rtmp_play_leave(s);

next:
    return next_close_stream(s, v);
}


static ngx_int_t
ngx_rtmp_play_seek(ngx_rtmp_session_t *s, ngx_rtmp_seek_t *v)
{
    ngx_rtmp_play_ctx_t            *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);
    if (ctx == NULL || ctx->file.fd == NGX_INVALID_FILE) {
        goto next;
    }

    if (!ctx->opened) {
        ctx->post_seek = (ngx_uint_t) v->offset;
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "play: post seek=%ui", ctx->post_seek);
        goto next;
    }

    if (ngx_rtmp_send_stream_eof(s, NGX_RTMP_MSID) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_rtmp_play_do_seek(s, (ngx_uint_t) v->offset);

    if (ngx_rtmp_send_status(s, "NetStream.Seek.Notify", "status", "Seeking")
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_rtmp_send_stream_begin(s, NGX_RTMP_MSID) != NGX_OK) {
        return NGX_ERROR;
    }

next:
    return next_seek(s, v);
}


static ngx_int_t
ngx_rtmp_play_pause(ngx_rtmp_session_t *s, ngx_rtmp_pause_t *v)
{
    ngx_rtmp_play_ctx_t            *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx == NULL || ctx->file.fd == NGX_INVALID_FILE) {
        goto next;
    }

    if (!ctx->opened) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "play: pause ignored");
        goto next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: pause=%i timestamp=%f",
                   (ngx_int_t) v->pause, v->position);

    if (v->pause) {
        if (ngx_rtmp_send_status(s, "NetStream.Pause.Notify", "status",
                                 "Paused video on demand")
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        ngx_rtmp_play_do_stop(s);

    } else {
        if (ngx_rtmp_send_status(s, "NetStream.Unpause.Notify", "status",
                                 "Unpaused video on demand")
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        ngx_rtmp_play_do_start(s); /*TODO: v->position? */
    }

next:
    return next_pause(s, v);
}


static ngx_int_t
ngx_rtmp_play_parse_index(char type, u_char *args)
{
    u_char             *p, c;
    static u_char       name[] = "xindex=";

    name[0] = (u_char) type;

    for ( ;; ) {
        p = (u_char *) ngx_strstr(args, name);
        if (p == NULL) {
            return 0;
        }

        if (p != args) {
            c = *(p - 1);
            if (c != '?' && c != '&') {
                args = p + 1;
                continue;
            }
        }

        return atoi((char *) p + (sizeof(name) - 1));
    }
}


static ngx_int_t
ngx_rtmp_play_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    ngx_rtmp_play_main_conf_t      *pmcf;
    ngx_rtmp_play_app_conf_t       *pacf;
    ngx_rtmp_play_ctx_t            *ctx;
    u_char                         *p;
    ngx_rtmp_play_fmt_t            *fmt, **pfmt;
    ngx_str_t                      *pfx, *sfx;
    ngx_str_t                       name;
    ngx_uint_t                      n;

    pmcf = ngx_rtmp_get_module_main_conf(s, ngx_rtmp_play_module);

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);

    if (pacf == NULL || pacf->entries.nelts == 0) {
        goto next;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "play: play name='%s' timestamp=%i",
                  v->name, (ngx_int_t) v->start);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx && ctx->file.fd != NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                     "play: already playing");
        goto next;
    }

    /* check for double-dot in v->name;
     * we should not move out of play directory */
    for (p = v->name; *p; ++p) {
        if (ngx_path_separator(p[0]) &&
            p[1] == '.' && p[2] == '.' &&
            ngx_path_separator(p[3]))
        {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                         "play: bad name '%s'", v->name);
            return NGX_ERROR;
        }
    }

    if (ctx == NULL) {
        /* 分配 ngx_rtmp_play_module 模块的上下文结构体，并将其放入到全局数组 s->ctx 中 */
        ctx = ngx_palloc(s->connection->pool, sizeof(ngx_rtmp_play_ctx_t));
        ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_play_module);
    }

    ngx_memzero(ctx, sizeof(*ctx));

    ctx->session = s;
    ctx->aindex = ngx_rtmp_play_parse_index('a', v->args);
    ctx->vindex = ngx_rtmp_play_parse_index('v', v->args);

    ctx->file.log = s->connection->log;

    ngx_memcpy(ctx->name, v->name, NGX_RTMP_MAX_NAME);

    name.len = ngx_strlen(v->name);
    name.data = v->name;

    /* pmcf->fmts 数组是在 postconfiguration 方法中初始化好的，
     * 在 nginx-rtmp 中，该数组仅有两项，分别是 flv 和 mp4 */
    pfmt = pmcf->fmts.elts;

    for (n = 0; n < pmcf->fmts.nelts; ++n, ++pfmt) {
        fmt = *pfmt;

        pfx = &fmt->pfx;    // 封装格式的前缀
        sfx = &fmt->sfx;    // 封装格式的后缀

        if (pfx->len == 0 && ctx->fmt == NULL) {
            ctx->fmt = fmt;
        }

        if (pfx->len && name.len >= pfx->len &&
            ngx_strncasecmp(pfx->data, name.data, pfx->len) == 0)
        {
            ctx->pfx_size = pfx->len;
            ctx->fmt = fmt;

            break;
        }

        /* 比较客户端请求播放的文件的后缀与服务器支持的文件后缀是否一致 */
        if (name.len >= sfx->len &&
            ngx_strncasecmp(sfx->data, name.data + name.len - sfx->len,
                            sfx->len) == 0)
        {
            ctx->fmt = fmt;
        }
    }

    if (ctx->fmt == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "play: fmt not found");
        goto next;
    }

    ctx->file.fd = NGX_INVALID_FILE;
    ctx->nentry = NGX_CONF_UNSET_UINT;
    ctx->post_seek = NGX_CONF_UNSET_UINT;

    sfx = &ctx->fmt->sfx;

    if (name.len < sfx->len ||
        ngx_strncasecmp(sfx->data, name.data + name.len - sfx->len,
                        sfx->len))
    {
        ctx->sfx = *sfx;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: fmt=%V", &ctx->fmt->name);

    return ngx_rtmp_play_next_entry(s, v);

next:
    return next_play(s, v);
}


static ngx_int_t
ngx_rtmp_play_next_entry(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    ngx_rtmp_play_app_conf_t   *pacf;
    ngx_rtmp_play_ctx_t        *ctx;
    ngx_rtmp_play_entry_t      *pe;
    u_char                     *p;
    static u_char               path[NGX_MAX_PATH + 1];

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    for ( ;; ) {

        /* 若文件描述符不为 -1 ，则关闭它 */
        if (ctx->file.fd != NGX_INVALID_FILE) {
            ngx_close_file(ctx->file.fd);
            ctx->file.fd = NGX_INVALID_FILE;
        }

        if (ctx->file_id) {
            ngx_rtmp_play_cleanup_local_file(s);
        }

        ctx->nentry = (ctx->nentry == NGX_CONF_UNSET_UINT ?
                       0 : ctx->nentry + 1);

        if (ctx->nentry >= pacf->entries.nelts) {
            ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                           "play: all entries failed");

            ngx_rtmp_send_status(s, "NetStream.Play.StreamNotFound", "error",
                                 "Video on demand stream not found");
            break;
        }

        /* 根据 ctx->nentry 获取当前要播放的路径，该路径保存在 ngx_rtmp_play_entry_t，
         * 若播放的是本地文件，则路径保存在成员 root 中，若播放的是网络文件，则路径
         * 保存在 url 中 */
        pe = ngx_rtmp_play_get_current_entry(s);

        ngx_log_debug4(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "play: trying %s entry %ui/%uz '%V'",
                       pe->url ? "remote" : "local",
                       ctx->nentry + 1, pacf->entries.nelts,
                       pe->url ? &pe->url->url : pe->root);

        /* open remote */

        if (pe->url) {
            return ngx_rtmp_play_open_remote(s, v);
        }

        /* open local */

        p = ngx_snprintf(path, NGX_MAX_PATH, "%V/%s%V",
                         pe->root, v->name + ctx->pfx_size, &ctx->sfx);
        *p = 0;

        /* 打开要播放的本地文件 */
        ctx->file.fd = ngx_open_file(path, NGX_FILE_RDONLY, NGX_FILE_OPEN,
                                     NGX_FILE_DEFAULT_ACCESS);

        if (ctx->file.fd == NGX_INVALID_FILE) {
            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, ngx_errno,
                           "play: error opening file '%s'", path);
            continue;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "play: open local file '%s'", path);

        if (ngx_rtmp_play_open(s, v->start) != NGX_OK) {
            return NGX_ERROR;
        }

        break;
    }

    return next_play(s, v);
}


static ngx_int_t
ngx_rtmp_play_open(ngx_rtmp_session_t *s, double start)
{
    ngx_rtmp_play_ctx_t    *ctx;
    ngx_event_t            *e;
    ngx_uint_t              timestamp;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx->file.fd == NGX_INVALID_FILE) {
        return NGX_ERROR;
    }

    /* 向客户端发送 消息类型为NGX_RTMP_MSG_USER(4) 的 stream_begin 消息, 如下图[9] */
    if (ngx_rtmp_send_stream_begin(s, NGX_RTMP_MSID) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 然后接着发送状态消息，如下图[10] */
    if (ngx_rtmp_send_status(s, "NetStream.Play.Start", "status",
                             "Start video on demand")
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_rtmp_play_join(s) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 初始化该 send_evt 事件 */
    e = &ctx->send_evt;
    e->data = s;
    /* 初始化发送 指定播放文件数据的 回调函数 */
    e->handler = ngx_rtmp_play_send;
    e->log = s->connection->log;

    /* 发送 recored 命令，如下图[11] */
    ngx_rtmp_send_recorded(s, 1);

    /* 发送 sample access，如下图[12] */
    if (ngx_rtmp_send_sample_access(s) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 调用相应封装格式的 init 函数 */
    if (ngx_rtmp_play_do_init(s) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 计算时间戳，有下一步 seek 可知，其实就是计算发送 媒体文件的起始位置 */
    timestamp = ctx->post_seek != NGX_CONF_UNSET_UINT ? ctx->post_seek :
                (start < 0 ? 0 : (ngx_uint_t) start);

    /* 同理，调用相应封装格式的 seek 函数，将文件指针 seek 到 timstamp 位置（相对文件起始） */
    if (ngx_rtmp_play_do_seek(s, timestamp) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_rtmp_play_do_start(s) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 置位 opend 标志位，表示文件已经打开了 */
    ctx->opened = 1;

    return NGX_OK;
}
/*从 ngx_rtmp_play_open 函数返回后，会一路返回到 ngx_rtmp_recv 函数中，然后解析完接收到的下一个客户端
发送来的 user control message(4) 消息（在开始分析 play 时说过）后，就一路返回到
ngx_process_events_and_timers 函数中，即如下：*/


static ngx_chain_t *
ngx_rtmp_play_remote_create(ngx_rtmp_session_t *s, void *arg, ngx_pool_t *pool)
{
    ngx_rtmp_play_t                *v = arg;

    ngx_rtmp_play_ctx_t            *ctx;
    ngx_rtmp_play_entry_t          *pe;
    ngx_str_t                      *addr_text, uri;
    u_char                         *p, *name;
    size_t                          args_len, name_len, len;
    static ngx_str_t                text_plain = ngx_string("text/plain");

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    pe = ngx_rtmp_play_get_current_entry(s);

    name = v->name + ctx->pfx_size;

    name_len = ngx_strlen(name);
    args_len = ngx_strlen(v->args);
    addr_text = &s->connection->addr_text;

    len = pe->url->uri.len + 1 +
          name_len + ctx->sfx.len +
          sizeof("?addr=") + addr_text->len * 3 +
          1 + args_len;

    uri.data = ngx_palloc(pool, len);
    if (uri.data == NULL) {
        return NULL;
    }

    p = uri.data;

    p = ngx_cpymem(p, pe->url->uri.data, pe->url->uri.len);

    if (p == uri.data || p[-1] != '/') {
        *p++ = '/';
    }

    p = ngx_cpymem(p, name, name_len);
    p = ngx_cpymem(p, ctx->sfx.data, ctx->sfx.len);
    p = ngx_cpymem(p, (u_char*)"?addr=", sizeof("&addr=") -1);
    p = (u_char*)ngx_escape_uri(p, addr_text->data, addr_text->len,
                                NGX_ESCAPE_ARGS);
    if (args_len) {
        *p++ = '&';
        p = (u_char *) ngx_cpymem(p, v->args, args_len);
    }

    uri.len = p - uri.data;

    return ngx_rtmp_netcall_http_format_request(NGX_RTMP_NETCALL_HTTP_GET,
                                                &pe->url->host, &uri,
                                                NULL, NULL, pool, &text_plain);
}


static ngx_int_t
ngx_rtmp_play_remote_handle(ngx_rtmp_session_t *s, void *arg, ngx_chain_t *in)
{
    ngx_rtmp_play_t        *v = arg;

    ngx_rtmp_play_ctx_t    *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    if (ctx->nbody == 0) {
        return ngx_rtmp_play_next_entry(s, v);
    }

    if (ctx->file_id) {
        ngx_rtmp_play_copy_local_file(s, v->name);
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: open remote file");

    if (ngx_rtmp_play_open(s, v->start) != NGX_OK) {
        return NGX_ERROR;
    }

    return next_play(s, (ngx_rtmp_play_t *)arg);
}


static ngx_int_t
ngx_rtmp_play_remote_sink(ngx_rtmp_session_t *s, ngx_chain_t *in)
{
    ngx_rtmp_play_ctx_t    *ctx;
    ngx_buf_t              *b;
    ngx_int_t               rc;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    /* skip HTTP header */
    while (in && ctx->ncrs != 2) {
        b = in->buf;

        for (; b->pos != b->last && ctx->ncrs != 2; ++b->pos) {
            switch (*b->pos) {
                case '\n':
                    ++ctx->ncrs;
                case '\r':
                    break;
                default:
                    ctx->ncrs = 0;
            }
            /* 10th header byte is HTTP response header */
            if (++ctx->nheader == 10 && *b->pos != (u_char) '2') {
                ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                              "play: remote HTTP response code: %cxx",
                              *b->pos);
                return NGX_ERROR;
            }
        }

        if (b->pos == b->last) {
            in = in->next;
        }
    }

    /* write to temp file */
    for (; in; in = in->next) {
        b = in->buf;

        if (b->pos == b->last) {
            continue;
        }

        rc = ngx_write_fd(ctx->file.fd, b->pos, b->last - b->pos);

        if (rc == NGX_ERROR) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, ngx_errno,
                          "play: error writing to temp file");
            return NGX_ERROR;
        }

        ctx->nbody += rc;
    }

    return NGX_OK;
}


static ngx_rtmp_play_entry_t *
ngx_rtmp_play_get_current_entry(ngx_rtmp_session_t *s)
{
    ngx_rtmp_play_app_conf_t   *pacf;
    ngx_rtmp_play_ctx_t        *ctx;
    ngx_rtmp_play_entry_t     **ppe;

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    ppe = pacf->entries.elts;

    return ppe[ctx->nentry];
}


static ngx_int_t
ngx_rtmp_play_open_remote(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    ngx_rtmp_play_app_conf_t       *pacf;
    ngx_rtmp_play_ctx_t            *ctx;
    ngx_rtmp_play_entry_t          *pe;
    ngx_rtmp_netcall_init_t         ci;
    u_char                         *path;
    ngx_err_t                       err;
    static ngx_uint_t               file_id;

    pacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_play_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_play_module);

    ctx->ncrs = 0;
    ctx->nheader = 0;
    ctx->nbody = 0;

    for ( ;; ) {
        ctx->file_id = ++file_id;

        /* no zero after overflow */
        if (ctx->file_id == 0) {
            continue;
        }

        path = ngx_rtmp_play_get_local_file_path(s);

        ctx->file.fd = ngx_open_tempfile(path, pacf->local_path.len, 0);

        if (pacf->local_path.len == 0) {
            ctx->file_id = 0;
        }

        if (ctx->file.fd != NGX_INVALID_FILE) {
            break;
        }

        err = ngx_errno;

        if (err != NGX_EEXIST) {
            ctx->file_id = 0;

            ngx_log_error(NGX_LOG_INFO, s->connection->log, err,
                          "play: failed to create temp file");

            return NGX_ERROR;
        }
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "play: temp file '%s' file_id=%ui",
                   path, ctx->file_id);

    pe = ngx_rtmp_play_get_current_entry(s);

    ngx_memzero(&ci, sizeof(ci));

    ci.url = pe->url;
    ci.create = ngx_rtmp_play_remote_create;
    ci.sink   = ngx_rtmp_play_remote_sink;
    ci.handle = ngx_rtmp_play_remote_handle;
    ci.arg = v;
    ci.argsize = sizeof(*v);

    return ngx_rtmp_netcall_create(s, &ci);
}


static char *
ngx_rtmp_play_url(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_rtmp_play_app_conf_t       *pacf = conf;

    ngx_rtmp_play_entry_t          *pe, **ppe;
    ngx_str_t                       url;
    ngx_url_t                      *u;
    size_t                          add, n;
    ngx_str_t                      *value;

    /* 初始化 pacf->entries 数组 */
    if (pacf->entries.nalloc == 0 &&
        ngx_array_init(&pacf->entries, cf->pool, 1, sizeof(void *)) != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    for (n = 1; n < cf->args->nelts; ++n) {

        ppe = ngx_array_push(&pacf->entries);
        if (ppe == NULL) {
            return NGX_CONF_ERROR;
        }

        pe = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_play_entry_t));
        if (pe == NULL) {
            return NGX_CONF_ERROR;
        }

        *ppe = pe;

        /* 如果 "play" 配置项后的值，即 url 不为 "http://" 开始的，即为本地文件 */

        if (ngx_strncasecmp(value[n].data, (u_char *) "http://", 7)) {

            /* local file */

            pe->root = ngx_palloc(cf->pool, sizeof(ngx_str_t));
            if (pe->root == NULL) {
                return NGX_CONF_ERROR;
            }

            *pe->root = value[n];

            continue;
        }

        /* 否则，为网络文件 */
        
        /* http case */

        url = value[n];

        add = sizeof("http://") - 1;

        url.data += add;
        url.len  -= add;

        u = ngx_pcalloc(cf->pool, sizeof(ngx_url_t));
        if (u == NULL) {
            return NGX_CONF_ERROR;
        }

        u->url.len = url.len;
        u->url.data = url.data;
        u->default_port = 80;
        u->uri_part = 1;

        if (ngx_parse_url(cf->pool, u) != NGX_OK) {
            if (u->err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                        "%s in url \"%V\"", u->err, &u->url);
            }
            return NGX_CONF_ERROR;
        }

        pe->url = u;
    }

    return NGX_CONF_OK;
    /*该函数主要就是提取 "play" 配置项后的值，判断是本地文件还是网络文件，根据相应值构造 ngx_rtmp_play_entry_t，并将该指向该
    结构体的指针放入到 pacf->entries 数组中.
    下面继续看 ngx_rtmp_play_play 函数。*/
}


static ngx_int_t
ngx_rtmp_play_postconfiguration(ngx_conf_t *cf)
{
    next_play = ngx_rtmp_play;
    ngx_rtmp_play = ngx_rtmp_play_play;

    next_close_stream = ngx_rtmp_close_stream;
    ngx_rtmp_close_stream = ngx_rtmp_play_close_stream;

    next_seek = ngx_rtmp_seek;
    ngx_rtmp_seek = ngx_rtmp_play_seek;

    next_pause = ngx_rtmp_pause;
    ngx_rtmp_pause = ngx_rtmp_play_pause;

    return NGX_OK;
}
