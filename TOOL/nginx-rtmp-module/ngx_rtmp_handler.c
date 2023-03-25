
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp.h"
#include "ngx_rtmp_amf.h"


static void ngx_rtmp_recv(ngx_event_t *rev);
static void ngx_rtmp_send(ngx_event_t *rev);
static void ngx_rtmp_ping(ngx_event_t *rev);
static ngx_int_t ngx_rtmp_finalize_set_chunk_size(ngx_rtmp_session_t *s);


ngx_uint_t                  ngx_rtmp_naccepted;


ngx_rtmp_bandwidth_t        ngx_rtmp_bw_out;
ngx_rtmp_bandwidth_t        ngx_rtmp_bw_in;


#ifdef NGX_DEBUG
char*
ngx_rtmp_message_type(uint8_t type)
{
    static char*    types[] = {
        "?",
        "chunk_size",
        "abort",
        "ack",
        "user",
        "ack_size",
        "bandwidth",
        "edge",
        "audio",
        "video",
        "?",
        "?",
        "?",
        "?",
        "?",
        "amf3_meta",
        "amf3_shared",
        "amf3_cmd",
        "amf_meta",
        "amf_shared",
        "amf_cmd",
        "?",
        "aggregate"
    };

    return type < sizeof(types) / sizeof(types[0])
        ? types[type]
        : "?";
}


char*
ngx_rtmp_user_message_type(uint16_t evt)
{
    static char*    evts[] = {
        "stream_begin",
        "stream_eof",
        "stream dry",
        "set_buflen",
        "recorded",
        "",
        "ping_request",
        "ping_response",
    };

    return evt < sizeof(evts) / sizeof(evts[0])
        ? evts[evt]
        : "?";
}
#endif


void
ngx_rtmp_cycle(ngx_rtmp_session_t *s)
{
    ngx_connection_t           *c;

    c = s->connection;
    // 重新设置当前 rtmp 连接的读、写事件的回调函数。
    c->read->handler =  ngx_rtmp_recv; // 当监听到客户端发送的数据时，将调用 ngx_rtmp_recv 函数进行处理
    c->write->handler = ngx_rtmp_send;

    /* 初始化该会话的 ping_evt 事件，当网络出现问题时，
     * 就会尝试调用该事件的回调函数 ngx_rtmp_ping */
    s->ping_evt.data = c;
    s->ping_evt.log = c->log;
    s->ping_evt.handler = ngx_rtmp_ping;
    /* 将 ping_evt 添加到定时器中 */
    ngx_rtmp_reset_ping(s);

    /*  在 ngx_rtmp_recv 函数中，会循环接收客户端发来的 rtmp 包数据，接收到完整的一个 rtmp message 后，会根据该消息
        的 rtmp message type，调用相应的函数进行处理，如，若为 20，即为 amf0 类型的命令消息，就会调用
        ngx_rtmp_amf_message_handler 函数进行处理。 
    */
    ngx_rtmp_recv(c->read);
    /* 开始接收客户端发来的rtmp数据，若接收不到客户端的数据时，则将读事件添加
     * 到epoll中，并设置回调函数为ngx_rtmp_recv方法，当再次监听到客户端发来
     * 数据时会再次调用该方法进行处理 */
}


static ngx_chain_t *
ngx_rtmp_alloc_in_buf(ngx_rtmp_session_t *s)
{
    ngx_chain_t        *cl;
    ngx_buf_t          *b;
    size_t              size;

    if ((cl = ngx_alloc_chain_link(s->in_pool)) == NULL
       || (cl->buf = ngx_calloc_buf(s->in_pool)) == NULL)
    {
        return NULL;
    }

    cl->next = NULL;
    b = cl->buf;
    size = s->in_chunk_size + NGX_RTMP_MAX_CHUNK_HEADER;

    b->start = b->last = b->pos = ngx_palloc(s->in_pool, size);
    if (b->start == NULL) {
        return NULL;
    }
    b->end = b->start + size;

    return cl;
}


void
ngx_rtmp_reset_ping(ngx_rtmp_session_t *s)
{
    ngx_rtmp_core_srv_conf_t   *cscf;

    /* 获取该 server{} 下 ngx_rtmp_core_module 模块的 srv 级别的配置结构体  */
    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);
    if (cscf->ping == 0) {
        return;
    }

    s->ping_active = 0;
    s->ping_reset = 0;
    /* 将 ping_evt 添加到定时器中，定时时间为 cscf->ping，
     * 若配置文件中没有配置有，则默认时间为 60000 ms */
    ngx_add_timer(&s->ping_evt, cscf->ping);

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "ping: wait %Mms", cscf->ping);
}


static void
ngx_rtmp_ping(ngx_event_t *pev)
{
    ngx_connection_t           *c;
    ngx_rtmp_session_t         *s;
    ngx_rtmp_core_srv_conf_t   *cscf;

    c = pev->data;
    s = c->data;

    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    /* i/o event has happened; no need to ping */
    if (s->ping_reset) {
        ngx_rtmp_reset_ping(s);
        return;
    }

    if (s->ping_active) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                "ping: unresponded");
        ngx_rtmp_finalize_session(s);
        return;
    }

    if (cscf->busy) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                "ping: not busy between pings");
        ngx_rtmp_finalize_session(s);
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "ping: schedule %Mms", cscf->ping_timeout);

    if (ngx_rtmp_send_ping_request(s, (uint32_t)ngx_current_msec) != NGX_OK) {
        ngx_rtmp_finalize_session(s);
        return;
    }

    s->ping_active = 1;
    ngx_add_timer(pev, cscf->ping_timeout);
}


static void
ngx_rtmp_recv(ngx_event_t *rev)
{
    ngx_int_t                   n;
    ngx_connection_t           *c;
    ngx_rtmp_session_t         *s;
    ngx_rtmp_core_srv_conf_t   *cscf;
    ngx_rtmp_header_t          *h;
    ngx_rtmp_stream_t          *st, *st0;
    ngx_chain_t                *in, *head;
    ngx_buf_t                  *b;
    u_char                     *p, *pp, *old_pos;
    size_t                      size, fsize, old_size;
    uint8_t                     fmt, ext;
    uint32_t                    csid, timestamp;

    c = rev->data;
    s = c->data;
    b = NULL;
    old_pos = NULL;
    old_size = 0;
    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    /* 判断该连接是否已被销毁 */
    if (c->destroyed) {
        return;
    }

    for( ;; ) {

        st = &s->in_streams[s->in_csid];

        /* allocate new buffer */
        if (st->in == NULL) {
            /* 为类型为 ngx_chain_t 的结构体指针 st->in 分配内存  */
            st->in = ngx_rtmp_alloc_in_buf(s);
            if (st->in == NULL) {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                        "in buf alloc failed");
                ngx_rtmp_finalize_session(s);
                return;
            }
        }

        h  = &st->hdr;
        in = st->in;
        b  = in->buf;

        if (old_size) {

            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, c->log, 0,
                    "reusing formerly read data: %d", old_size);

            /* 对上一次接收到的数据按一个 chunk size 进行切分之后，
             * 余下的多余的数据再次循环时进行重新使用 */
            b->pos = b->start;

            size = ngx_min((size_t) (b->end - b->start), old_size);
            b->last = ngx_movemem(b->pos, old_pos, size);

            if (s->in_chunk_size_changing) {
                ngx_rtmp_finalize_set_chunk_size(s);
            }

        } else {

            if (old_pos) {
                b->pos = b->last = b->start;
            }

            /* 这里调用的回调函数为 ngx_unix_recv 方法接收数据 */
            n = c->recv(c, b->last, b->end - b->last);

            if (n == NGX_ERROR || n == 0) {
                ngx_rtmp_finalize_session(s);
                return;
            }

            if (n == NGX_AGAIN) {
                if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                    ngx_rtmp_finalize_session(s);
                }
                return;
            }

            s->ping_reset = 1;
            ngx_rtmp_update_bandwidth(&ngx_rtmp_bw_in, n);
            /* 更新缓存区的数据指针 */
            b->last += n;
            s->in_bytes += n;

            /* 当缓存区中数据满溢时 */
            if (s->in_bytes >= 0xf0000000) {
                ngx_log_debug0(NGX_LOG_DEBUG_RTMP, c->log, 0,
                               "resetting byte counter");
                s->in_bytes = 0;
                s->in_last_ack = 0;
            }

            if (s->ack_size && s->in_bytes - s->in_last_ack >= s->ack_size) {

                s->in_last_ack = s->in_bytes;

                ngx_log_debug1(NGX_LOG_DEBUG_RTMP, c->log, 0,
                        "sending RTMP ACK(%uD)", s->in_bytes);

                if (ngx_rtmp_send_ack(s, s->in_bytes)) {
                    ngx_rtmp_finalize_session(s);
                    return;
                }
            }
        }

        old_pos = NULL;
        old_size = 0;

        /* parse headers */
        if (b->pos == b->start) {
            /* 最初的时候:    b->pos == b->start == b->last */

            /**
             * Chunk format:
             * +----------------+------------------+--------------------+-------------+
             * | Basic Header   | Message Header   | Extended Timestamp | Chunk Data  |
             * +----------------+------------------+--------------------+-------------+
             */
            p = b->pos;
            /* Basic Header:基本头信息
             * 
             * 包含chunk stream ID（流通道id）和chunk type（即fmt），chunk stream id一般被
             * 简写为CSID，用来唯一标识一个特定的流通道，chunk type决定了后面Message Header的格式。
             * Basic Header的长度可能是1，2，或3个字节，其中chunk type的长度是固定的（占2位，单位
             * 是bit），Basic Header的长度取决于CSID的大小，在足够存储这两个字段的前提下最好用尽量
             * 少的字节从而减少由于引入Header增加的数据量。
             *
             * RTMP协议支持用户自定义[3,65599]之间的CSID，0,1,2由协议保留表示特殊信息。0代表Basic
             * Header总共要占用2个字节，CSID在[64,319]之间; 1代表占用3个字节，CSID在[64,65599]之
             * 间; 2 代表该 chunk 是控制信息和一些命令信息。
             *
             * chunk type的长度是固定2 bit，因此CSID的长度是(6=8-2)、(14=16-2)、(22=24-2)中的一
             * 个。当Basic Header为1个字节时，CSID占6bit，6bit最多可以表示64个数，因此在这种情况
             * 下CSID在[0,63]之间，其中用户可自定义的范围为[3,63]。
             *
             * Basic Header：1 byte
             *  0 1 2 3 4 5 6 7
             * +-+-+-+-+-+-+-+-+
             * |fmt|   cs id   |
             * +-+-+-+-+-+-+-+-+
             *
             * Basic Header: 2 byte , csid == 0
             * CSID占14bit，此时协议将于chunk type所在字节的其他bit都置为0，
             * 剩下的一个字节表示CSID - 64，这样共有8个bit来存储CSID，8bit可以表示[0,255]个数，因此
             * 这种情况下CSID在[64,319]，其中319=255+64。
             *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |fmt|    0      |  cs id - 64   |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             *
             * Basic Header: 3 bytes , csid == 1
             * CSID占22bit，此时协议将第一个字节的[2,8]bit置1，余下的16个bit表示CSID - 64，这样共有
             * 16个bit来存储CSID，16bit可以表示[0,65535]共 65536 个数，因此这种情况下 CSID 在 
             * [64,65599]，其中65599=65535+64，需要注意的是，Basic Header是采用小端存储的方式，越往
             * 后的字节数量级越高，因此通过3个字节的每一个bit的值来计算CSID时，应该是: 
             * <第三个字节的值> * 256 + <第二个字节的值> + 64.
             *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |fmt|    1      |          cs id - 64           |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             */

            /* chunk basic header */
            fmt  = (*p >> 6) & 0x03;
            csid = *p++ & 0x3f;

            /* csid 为 0 表示 Basic Header 占 2 bytes */
            if (csid == 0) {
                if (b->last - p < 1)
                    continue;
                csid = 64;
                csid += *(uint8_t*)p++;

            } 
            /* csid 为 1 表示 Basic Header 占 3 bytes */
            else if (csid == 1) {
                if (b->last - p < 2)
                    continue;
                csid = 64;
                csid += *(uint8_t*)p++;
                csid += (uint32_t)256 * (*(uint8_t*)p++);
            }

            ngx_log_debug2(NGX_LOG_DEBUG_RTMP, c->log, 0,
                    "RTMP bheader fmt=%d csid=%D",
                    (int)fmt, csid);

            if (csid >= (uint32_t)cscf->max_streams) {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                    "RTMP in chunk stream too big: %D >= %D",
                    csid, cscf->max_streams);
                ngx_rtmp_finalize_session(s);
                return;
            }

            /* link orphan */
            if (s->in_csid == 0) {

                /* unlink from stream #0 */
                st->in = st->in->next;

                /* link to new stream */
                s->in_csid = csid;
                st = &s->in_streams[csid];
                if (st->in == NULL) {
                    in->next = in;
                } else {
                    in->next = st->in->next;
                    st->in->next = in;
                }
                st->in = in;
                h = &st->hdr;
                h->csid = csid;
            }

            /* Message Header:
             * 
             * 包含了要发送的实际信息（可能是完整的，也可能是一部分）的描述信息。Message Header的格式
             * 和长度取决于Basic Header的chunk type，即fmt，共有四种不同的格式。其中第一种格式可以表
             * 示其他三种表示的所有数据，但由于其他三种格式是基于对之前chunk的差量化的表示，因此可以
             * 更简洁地表示相同的数据，实际使用的时候还是应该采用尽量少的字节表示相同意义的数据。下面
             * 按字节从多到少的顺序分别介绍这四种格式的 Message Header。
             *
             * 下面是 Message Header 四种消息头格式。
             * 
             * 一、Chunk Type(fmt) = 0：11 bytes
             *  0               1               2               3
             *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |                    timestamp                  |message length |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |    message length (coutinue)  |message type id| msg stream id |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |                  msg stream id                |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * type=0时Message Header占用11个字节，其他三种能表示的数据它都能表示，但在chunk stream
             * 的开始第一个chunk和头信息中的时间戳后退（即值与上一个chunk相比减小，通常在回退播放的
             * 时候会出现这种情况）的时候必须采用这种格式。
             * - timestamp（时间戳）：占用3个字节，因此它最多能表示到16777215=0xFFFFFF=2^24-1，当它
             *   的值超过这个最大值时，这三个字节都置为1，这样实际的timestamp会转存到 Extended 
             *   Timestamp 字段中，接收端在判断timestamp字段24个位都为1时就会去Extended Timestamp
             *   中解析实际的时间戳。
             * - message length（消息数据长度）：占用3个字节，表示实际发送的消息的数据如音频帧、视频
             *   帧等数据的长度，单位是字节。注意这里是Message的长度，也就是chunk属于的Message的总长
             *   度，而不是chunk本身data的长度。
             * - message type id(消息的类型id)：1个字节，表示实际发送的数据的类型，如8代表音频数据，
             *   9代表视频数据。
             * - message stream id(消息的流id)：4个字节，表示该chunk所在的流的ID，和Basic Header
             *   的CSID一样，它采用小端存储方式。
             *
             * 二、Chunk Type(fmt) = 1：7 bytes
             *  0               1               2               3
             *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |               timestamp delta                 |message length |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |    message length (coutinue)  |message type id|
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * type为1时占用7个字节，省去了表示message stream id的4个字节，表示此chunk和上一次发的
             * chunk所在的流相同，如果在发送端和对端有一个流链接的时候可以尽量采取这种格式。
             * - timestamp delta：3 bytes，这里和type=0时不同，存储的是和上一个chunk的时间差。类似
             *   上面提到的timestamp，当它的值超过3个字节所能表示的最大值时，三个字节都置为1，实际
             *   的时间戳差值就会转存到Extended Timestamp字段中，接收端在判断timestamp delta字段24
             *   个bit都为1时就会去Extended Timestamp 中解析实际的与上次时间戳的差值。
             * - 其他字段与上面的解释相同.
             *
             * 三、Chunk Type(fmt) = 2：3 bytes
             * 0               1               2               
             *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * |               timestamp delta                 |
             * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
             * type为2时占用3个字节，相对于type=1格式又省去了表示消息长度的3个字节和表示消息
             * 类型的1个字节，表示此chunk和上一次发送的chunk所在的流、消息的长度和消息的类型都相同。
             * 余下的这三个字节表示timestamp delta，使用同type=1。
             * 
             *
             * 四、Chunk Type(fmt) = 3: 0 byte
             * type=3时，为0字节，表示这个chunk的Message Header和上一个是完全相同的。当它跟在type=0
             * 的chunk后面时，表示和前一个chunk的时间戳都是相同。什么时候连时间戳都是相同呢？就是
             * 一个Message拆分成多个chunk，这个chunk和上一个chunk同属于一个Message。而当它跟在
             * type = 1或 type = 2 的chunk后面时的chunk后面时，表示和前一个chunk的时间戳的差是相同
             * 的。比如第一个 chunk 的 type = 0，timestamp = 100，第二个 chunk 的 type = 2，
             * timestamp delta = 20，表示时间戳为 100 + 20 = 120，第三个 chunk 的 type = 3，
             * 表示 timestamp delta = 20 时间戳为 120 + 20 = 140。
             */

            ext = st->ext;
            timestamp = st->dtime;
            if (fmt <= 2 ) {
                if (b->last - p < 3)
                    continue;
                /* timestamp:
                 *  big-endian 3b -> little-endian 4b */
                pp = (u_char*)&timestamp;
                pp[2] = *p++;
                pp[1] = *p++;
                pp[0] = *p++;
                pp[3] = 0;

                ext = (timestamp == 0x00ffffff);

                if (fmt <= 1) {
                    if (b->last - p < 4)
                        continue;
                    /* size:
                     *  big-endian 3b -> little-endian 4b
                     * type:
                     *  1b -> 1b*/
                    pp = (u_char*)&h->mlen;
                    pp[2] = *p++;
                    pp[1] = *p++;
                    pp[0] = *p++;
                    pp[3] = 0;
                    h->type = *(uint8_t*)p++;

                    if (fmt == 0) {
                        if (b->last - p < 4)
                            continue;
                        /* stream:
                         *  little-endian 4b -> little-endian 4b */
                        pp = (u_char*)&h->msid;
                        pp[0] = *p++;
                        pp[1] = *p++;
                        pp[2] = *p++;
                        pp[3] = *p++;
                    }
                }
            }

            /* 
             * Extended Timestamp(扩展时间戳)：
             * 在 chunk 中会有时间戳 timestamp 和时间戳差 timestamp delta，
             * 并且它们不会同时存在，只有这两者之一大于3字节能表示的最大数值
             * 0xFFFFFF＝16777215时，才会用这个字段来表示真正的时间戳，否则
             * 这个字段为 0。扩展时间戳占 4 个字节，能表示的最大数值就是
             * 0xFFFFFFFF＝4294967295。当扩展时间戳启用时，timestamp字段或者
             * timestamp delta要全置为1，而不是减去时间戳或者时间戳差的值。
             */

            /* extended header */
            if (ext) {
                if (b->last - p < 4)
                    continue;
                pp = (u_char*)&timestamp;
                pp[3] = *p++;
                pp[2] = *p++;
                pp[1] = *p++;
                pp[0] = *p++;
            }

            if (st->len == 0) {
                /* Messages with type=3 should
                 * never have ext timestamp field
                 * according to standard.
                 * However that's not always the case
                 * in real life */
                st->ext = (ext && cscf->publish_time_fix);
                if (fmt) {
                    st->dtime = timestamp;
                } else {
                    h->timestamp = timestamp;
                    st->dtime = 0;
                }
            }

            ngx_log_debug8(NGX_LOG_DEBUG_RTMP, c->log, 0,
                    "RTMP mheader fmt=%d %s (%d) "
                    "time=%uD+%uD mlen=%D len=%D msid=%D",
                    (int)fmt, ngx_rtmp_message_type(h->type), (int)h->type,
                    h->timestamp, st->dtime, h->mlen, st->len, h->msid);

            /* header done */
            /* 更新缓存区pos指针的位置，更新后pos应指向rtmp trunk的实际数据 */
            b->pos = p;

            if (h->mlen > cscf->max_message) {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                        "too big message: %uz", cscf->max_message);
                ngx_rtmp_finalize_session(s);
                return;
            }
        }

        /* b->last：指向本次recv到的数据的尾部
         * b->pos: 指向rtmp trunk的实际data起始
         * 因此，size为本次接收到的实际数据的大小
         */
        size = b->last - b->pos;
        /* h->mlen：该RTMP message的长度 
         * st->len：该流的长度
         */
        fsize = h->mlen - st->len;

        if (size < ngx_min(fsize, s->in_chunk_size))
            continue;

        /* buffer is ready */

        /* 本次所要接收的 rtmp message 的大小大于 s->in_shunk_size 块的大小
         * 因此要进行切分 */
        if (fsize > s->in_chunk_size) {
            /* collect fragmented chunks */
            st->len += s->in_chunk_size;
            b->last = b->pos + s->in_chunk_size;
            old_pos = b->last;
            /* 切分一个块后余下的数据大小 */
            old_size = size - s->in_chunk_size;

        } else {
            /* 完整接收一个 rtmp message 后，进行处理 */
            /* handle! */
            head = st->in->next;
            st->in->next = NULL;
            b->last = b->pos + fsize;
            old_pos = b->last;
            old_size = size - fsize;
            st->len = 0;
            h->timestamp += st->dtime;

            /* 根据该 rtmp 消息的类型调用相应的回调函数进行处理 */
            if (ngx_rtmp_receive_message(s, h, head) != NGX_OK) { // 通过msg的类型判断调用哪一个handler
                ngx_rtmp_finalize_session(s);
                return;
            }

            if (s->in_chunk_size_changing) {
                /* copy old data to a new buffer */
                if (!old_size) {
                    ngx_rtmp_finalize_set_chunk_size(s);
                }

            } else {
                /* add used bufs to stream #0 */
                st0 = &s->in_streams[0];
                st->in->next = st0->in;
                st0->in = head;
                st->in = NULL;
            }
        }

        s->in_csid = 0;
    }
}


static void
ngx_rtmp_send(ngx_event_t *wev)
{
    ngx_connection_t           *c;
    ngx_rtmp_session_t         *s;
    ngx_int_t                   n;
    ngx_rtmp_core_srv_conf_t   *cscf;

    c = wev->data;
    s = c->data;

    if (c->destroyed) {
        return;
    }

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT,
                "client timed out");
        c->timedout = 1;
        ngx_rtmp_finalize_session(s);
        return;
    }

    if (wev->timer_set) {
        ngx_del_timer(wev);
    }

    if (s->out_chain == NULL && s->out_pos != s->out_last) {
        /* 将保存着将要发送的 rtmp 包赋给 s->out_chain */
        s->out_chain = s->out[s->out_pos];
        s->out_bpos = s->out_chain->buf->pos;
    }

    while (s->out_chain) {
        /* 调用 ngx_unix_send 回调函数发送 */
        n = c->send(c, s->out_bpos, s->out_chain->buf->last - s->out_bpos);

        if (n == NGX_AGAIN || n == 0) {
            ngx_add_timer(c->write, s->timeout);
            if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
                ngx_rtmp_finalize_session(s);
            }
            return;
        }

        if (n < 0) {
            ngx_rtmp_finalize_session(s);
            return;
        }

        s->out_bytes += n;
        s->ping_reset = 1;
        ngx_rtmp_update_bandwidth(&ngx_rtmp_bw_out, n);
        s->out_bpos += n;
        /* 若已经完全发送 */
        if (s->out_bpos == s->out_chain->buf->last) {
            /* 指向下一个 ngx_chian_t */
            s->out_chain = s->out_chain->next;
            /* 若不存在 */
            if (s->out_chain == NULL) {
                cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);
                /* 则释放之前已经发送完成的 ngx_chain_t */
                ngx_rtmp_free_shared_chain(cscf, s->out[s->out_pos]);
                ++s->out_pos;
                s->out_pos %= s->out_queue;
                /* 若相等，则表明所有消息的都发送了 */
                if (s->out_pos == s->out_last) {
                    break;
                }
                s->out_chain = s->out[s->out_pos];
            }
            s->out_bpos = s->out_chain->buf->pos;
        }
    }

    /* 若当前 写事件 是活跃的，则将其从 epoll 等事件监控机制中删除 */
    if (wev->active) {
        ngx_del_event(wev, NGX_WRITE_EVENT, 0);
    }

    /* 将 posted_dry_events 延迟队列上的事件都移除，并执行 */
    ngx_event_process_posted((ngx_cycle_t *) ngx_cycle, &s->posted_dry_events);
}


void
ngx_rtmp_prepare_message(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_rtmp_header_t *lh, ngx_chain_t *out)
{
    ngx_chain_t                *l;
    u_char                     *p, *pp;
    ngx_int_t                   hsize, thsize, nbufs;
    uint32_t                    mlen, timestamp, ext_timestamp;
    static uint8_t              hdrsize[] = { 12, 8, 4, 1 };
    u_char                      th[7];
    ngx_rtmp_core_srv_conf_t   *cscf;
    uint8_t                     fmt;
    ngx_connection_t           *c;

    c = s->connection;
    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    /* 检测 csid 是否太大 */
    if (h->csid >= (uint32_t)cscf->max_streams) {
        ngx_log_error(NGX_LOG_INFO, c->log, 0,
                "RTMP out chunk stream too big: %D >= %D",
                h->csid, cscf->max_streams);
        ngx_rtmp_finalize_session(s);
        return;
    }

    /* 检测输出缓存的总大小 */
    /* detect packet size */
    mlen = 0;
    nbufs = 0;
    for(l = out; l; l = l->next) {
        mlen += (l->buf->last - l->buf->pos);
        ++nbufs;
    }

    /* 若当前发送的是 RTMP 消息的第一个 chunk 时，fmt 必须为 0 */
    fmt = 0;

    /* 下面的判断是检测是否分块，若分块了，则头部中相应的字段就有变化了 */

    /* 若这两个 chunk：lh 和 h 是在同一个消息的流中，即 msid 相同 */
    if (lh && lh->csid && h->msid == lh->msid) {
        /* fmt 加 1，此时 Message Header 需 7 bytes */
        ++fmt;
        /* 若不仅在同一个流中，且 chunk 的长度和消息类型都相同 */
        if (h->type == lh->type && mlen && mlen == lh->mlen) {
            /*  fmt 再加 1，此时表示 Message Header 仅需 3 bytes，即 timestamp delta  */
            ++fmt;
            if (h->timestamp == lh->timestamp) {
                /* fmt 再加 1，此时表示这个 chunk 和上一个完全相同的，
                 * 即 不需要 Message Header 了，为 0 byte */
                ++fmt;
            }
        }
        /* 这是计算当前 chunk 和上一个 chunk 的时间戳差值，即 timestamp delta  */
        timestamp = h->timestamp - lh->timestamp;
    } else {
        /* 这里表示这是第一个 chunk 或者是不需要分块的 */
        timestamp = h->timestamp;
    }

    /*if (lh) {
        *lh = *h;
        lh->mlen = mlen;
    }*/

    /* 根据 fmt 得出 rtmp 消息头的实际大小，这里默认 rtmp 的 Basic Header 大小为 1 byte */
    hsize = hdrsize[fmt];

    ngx_log_debug8(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "RTMP prep %s (%d) fmt=%d csid=%uD timestamp=%uD "
            "mlen=%uD msid=%uD nbufs=%d",
            ngx_rtmp_message_type(h->type), (int)h->type, (int)fmt,
            h->csid, timestamp, mlen, h->msid, nbufs);

    /* 这里只有当 timestamp 大于 3 字节能表示的最大数值时，才会有 扩展时间戳 */
    ext_timestamp = 0;
    if (timestamp >= 0x00ffffff) {
        /* 若需要用到扩展时间戳，则 ext_timestamp 表示真正的时间戳，而 timestamp 字段全置为 1 */
        ext_timestamp = timestamp;
        timestamp = 0x00ffffff;
        /* 扩展时间戳占 4 bytes */
        hsize += 4;
    }

    /* 若 csid 大于 64，则表示 Basic Header 需要 2 个字节，此时 csid 范围 [64, 319] */
    if (h->csid >= 64) {
        ++hsize;
        /* 若 csid 大于 320，则表示 Basic Header 需要 3 个字节，此时 csid 范围 [64, 65599] */
        if (h->csid >= 320) {
            ++hsize;
        }
    }

    /* 将 out->buf->pos 指针指向 rtmp 消息的头部起始处 */
    /* fill initial header */
    out->buf->pos -= hsize;
    p = out->buf->pos;

    /* basic header */
    *p = (fmt << 6);
    if (h->csid >= 2 && h->csid <= 63) {
        *p++ |= (((uint8_t)h->csid) & 0x3f);
    } else if (h->csid >= 64 && h->csid < 320) {
        ++p;
        *p++ = (uint8_t)(h->csid - 64);
    } else {
        *p++ |= 1;
        *p++ = (uint8_t)(h->csid - 64);
        *p++ = (uint8_t)((h->csid - 64) >> 8);
    }

    /* create fmt3 header for successive fragments */
    thsize = p - out->buf->pos;
    ngx_memcpy(th, out->buf->pos, thsize);
    /* 提取出 fmt 值 */
    th[0] |= 0xc0;

    /* message header */
    if (fmt <= 2) {
        /* 此时 timestamp 存在*/
        pp = (u_char*)&timestamp;
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
        if (fmt <= 1) {
            /* 此时 message type id 和 message length 存在 */
            pp = (u_char*)&mlen;
            *p++ = pp[2];
            *p++ = pp[1];
            *p++ = pp[0];
            *p++ = h->type;
            if (fmt == 0) {
                /* 此时 message stream id 存在 */
                pp = (u_char*)&h->msid;
                *p++ = pp[0];
                *p++ = pp[1];
                *p++ = pp[2];
                *p++ = pp[3];
            }
        }
    }

    /* 若 fmt 大于 2，则说明没有 message header */
    
    /* extended header */
    if (ext_timestamp) {
        pp = (u_char*)&ext_timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];

        /* This CONTRADICTS the standard
         * but that's the way flash client
         * wants data to be encoded;
         * ffmpeg complains */
        if (cscf->play_time_fix) {
            ngx_memcpy(&th[thsize], p - 4, 4);
            thsize += 4;
        }
    }

    /* append headers to successive fragments */
    for(out = out->next; out; out = out->next) {
        out->buf->pos -= thsize;
        ngx_memcpy(out->buf->pos, th, thsize);
    }
}


ngx_int_t
ngx_rtmp_send_message(ngx_rtmp_session_t *s, ngx_chain_t *out,
        ngx_uint_t priority)
{
    ngx_uint_t                      nmsg;

    /* 计算要发送的消息数 */
    nmsg = (s->out_last - s->out_pos) % s->out_queue + 1;

    if (priority > 3) {
        priority = 3;
    }

    /* drop packet? 丢包
     * Note we always leave 1 slot free */
    if (nmsg + priority * s->out_queue / 4 >= s->out_queue) {
        ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                "RTMP drop message bufs=%ui, priority=%ui",
                nmsg, priority);
        return NGX_AGAIN;
    }

    s->out[s->out_last++] = out;
    s->out_last %= s->out_queue;

    /* out 的引用计数加 1 */
    ngx_rtmp_acquire_shared_chain(out);

    ngx_log_debug3(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "RTMP send nmsg=%ui, priority=%ui #%ui",
            nmsg, priority, s->out_last);

    if (priority && s->out_buffer && nmsg < s->out_cork) {
        return NGX_OK;
    }

    /* 若当前连接的 write 事件还没有活跃时，发送该 rtmp 包 */
    if (!s->connection->write->active) {
        ngx_rtmp_send(s->connection->write);
        /*return ngx_add_event(s->connection->write, NGX_WRITE_EVENT, NGX_CLEAR_EVENT);*/
    }

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_receive_message(ngx_rtmp_session_t *s,
        ngx_rtmp_header_t *h, ngx_chain_t *in)
{
    ngx_rtmp_core_main_conf_t  *cmcf;
    ngx_array_t                *evhs;
    size_t                      n;
    ngx_rtmp_handler_pt        *evh;

    cmcf = ngx_rtmp_get_module_main_conf(s, ngx_rtmp_core_module);

#ifdef NGX_DEBUG
    {
        int             nbufs;
        ngx_chain_t    *ch;

        for(nbufs = 1, ch = in;
                ch->next;
                ch = ch->next, ++nbufs);

        ngx_log_debug7(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                "RTMP recv %s (%d) csid=%D timestamp=%D "
                "mlen=%D msid=%D nbufs=%d",
                ngx_rtmp_message_type(h->type), (int)h->type,
                h->csid, h->timestamp, h->mlen, h->msid, nbufs);
    }
#endif

    if (h->type > NGX_RTMP_MSG_MAX) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                "unexpected RTMP message type: %d", (int)h->type);
        return NGX_OK;
    }

    evhs = &cmcf->events[h->type]; // 根据消息的类型取出相对应的 handler 
    evh = evhs->elts;

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "nhandlers: %d", evhs->nelts);

    for(n = 0; n < evhs->nelts; ++n, ++evh) {
        if (!evh) {
            continue;
        }
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                "calling handler %d", n);

        /* 若接收到的是 amf 类型的事件，则对应的处理函数为 ngx_rtmp_amf_message_handler；
         * 若接收到的是 standard protocol 类型事件，则对应的处理函数为 
         * ngx_rtmp_protocol_message_handler；
         * 若接收到的是 user protocol 事件，则对应的处理函数为 ngx_rtmp_user_message_handler；
         */
        switch ((*evh)(s, h, in)) { // 比如该消息类型为 20，即为 AMF0 Command，因此会调用 ngx_rtmp_amf_message_handler 对该消息进行解析
            case NGX_ERROR:
                ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                        "handler %d failed", n);
                return NGX_ERROR;
            case NGX_DONE:
                return NGX_OK;
        }
        /* 如上图，接收到客户端发来的第一个 RTMP message 的类型为 20，即 NGX_RTMP_MSG_AMF_CMD，因此将会调用之前由
        ngx_rtmp_init_event_handlers() 方法对该类型设置的回调方法 ngx_rtmp_amf_message_handler。并且，由上图可
        知，客户端发送的是 connect 连接，因此，在 ngx_rtmp_amf_message_handler 主要做的就是：
        从接收到的 amf 数据中提取出第一个字符串，也即上图的 "connect"；
        然后在 cmcf->amf_hash 指向的 hash 表中查找是否存储有该 "connect" 的处理函数，有则调用它，否则返回。*/
    }

    return NGX_OK;
}


/*该函数主要是设置 s->in_chunk_size（即 rtmp 块大小） 的大小，并为 s->in_pool 重新分配一个 4096 大小的内存池，
最后检测旧的内存池中是否有块数据，有则拷贝过来*/
ngx_int_t
ngx_rtmp_set_chunk_size(ngx_rtmp_session_t *s, ngx_uint_t size)
{
    // 服务器接收到客户端发送的设置块大小消息。此时服务器会调用到 ngx_rtmp_set_chunk_size 函数进行块大小的设置
    ngx_rtmp_core_srv_conf_t           *cscf;
    ngx_chain_t                        *li, *fli, *lo, *flo;
    ngx_buf_t                          *bi, *bo;
    ngx_int_t                           n;

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
        "setting chunk_size=%ui", size);

    if (size > NGX_RTMP_MAX_CHUNK_SIZE) {
        ngx_log_error(NGX_LOG_ALERT, s->connection->log, 0,
                      "too big RTMP chunk size:%ui", size);
        return NGX_ERROR;
    }

    /* 获取 server{} 所属的 ngx_rtmp_conf_ctx_t 的 srv_conf[0]，即对应的
     * ngx_rtmp_core_module 的srv级别配置结构体，也对应该 server{} 的 配置结构体 */
    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    s->in_old_pool = s->in_pool;
    /* 设置 rtmp 块的大小 */
    s->in_chunk_size = size;
    s->in_pool = ngx_create_pool(4096, s->connection->log);

    /* copy existing chunk data */
    if (s->in_old_pool) {
        s->in_chunk_size_changing = 1;
        s->in_streams[0].in = NULL;

        for(n = 1; n < cscf->max_streams; ++n) {
            /* stream buffer is circular
             * for all streams except for the current one
             * (which caused this chunk size change);
             * we can simply ignore it */
            li = s->in_streams[n].in;
            if (li == NULL || li->next == NULL) {
                s->in_streams[n].in = NULL;
                continue;
            }
            /* move from last to the first */
            li = li->next;
            fli = li;
            lo = ngx_rtmp_alloc_in_buf(s);
            if (lo == NULL) {
                return NGX_ERROR;
            }
            flo = lo;
            for ( ;; ) {
                bi = li->buf;
                bo = lo->buf;

                if (bo->end - bo->last >= bi->last - bi->pos) {
                    bo->last = ngx_cpymem(bo->last, bi->pos,
                            bi->last - bi->pos);
                    li = li->next;
                    if (li == fli)  {
                        lo->next = flo;
                        s->in_streams[n].in = lo;
                        break;
                    }
                    continue;
                }

                bi->pos += (ngx_cpymem(bo->last, bi->pos,
                            bo->end - bo->last) - bo->last);
                lo->next = ngx_rtmp_alloc_in_buf(s);
                lo = lo->next;
                if (lo == NULL) {
                    return NGX_ERROR;
                }
            }
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_finalize_set_chunk_size(ngx_rtmp_session_t *s)
{
    if (s->in_chunk_size_changing && s->in_old_pool) {
        ngx_destroy_pool(s->in_old_pool);
        s->in_old_pool = NULL;
        s->in_chunk_size_changing = 0;
    }
    return NGX_OK;
}


