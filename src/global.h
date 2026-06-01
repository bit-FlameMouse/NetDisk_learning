/**
 * global.h — 服务端全局状态
 *
 * 所有模块通过 include 本文件访问全局变量。
 * 变量定义在 main.c 中，此处仅 extern 声明。
 */

#ifndef GLOBAL_H
#define GLOBAL_H

#include "../common/config/config.h"
#include "base/thread_pool/thread_pool.h"
#include "data/db.h"
#include <stdint.h>

/* ========================================================================
 * 全局配置与基础设施
 * ======================================================================== */

extern config_t      *g_config;       /* 全局配置（main.c 加载） */
extern db_pool_t     *g_db_pool;      /* MySQL 连接池 */
extern thread_pool_t *g_thread_pool;  /* 工作线程池 */

/* ========================================================================
 * 存储相关
 * ======================================================================== */

extern char   g_data_dir[256];  /* 文件存储根目录 */
extern char   g_tmp_dir[256];   /* 临时文件目录 */
extern int    g_chunk_size;     /* 上传分片大小（字节） */

/* ========================================================================
 * 连接管理
 * ======================================================================== */

#define MAX_CONNECTIONS  10240
#define CONN_BUF_SIZE    (64 * 1024)    /* 单连接收发缓冲区 64KB */

/* 帧解析状态（对齐 DESIGN.md §5.4） */
typedef enum {
    FRAME_MAGIC1  = 0,   /* 等待 'N' (0x4E) */
    FRAME_MAGIC2  = 1,   /* 等待 'D' (0x44) */
    FRAME_HEADER  = 2,   /* 读剩余 6 字节帧头 */
    FRAME_PAYLOAD = 3,   /* 读 payload_len 字节载荷 */
    FRAME_DONE    = 4,   /* 帧完整，可投递处理 */
} frame_state_t;

/* 单连接上下文 */
typedef struct conn {
    int             fd;               /* 客户端 socket */
    int             user_id;          /* 登录后的用户 ID（0 = 未登录） */
    int             state;            /* CONN_HANDSHAKE / CONN_AUTHED */
    uint8_t         token[512];       /* JWT token（已认证连接） */

    /* ---- 帧解析状态 ---- */
    frame_state_t   frame_state;      /* 状态机当前状态 */
    uint8_t         header_buf[8];    /* 帧头缓冲区 */
    int             header_pos;       /* 已读帧头字节数 */
    uint8_t        *payload_buf;      /* 载荷缓冲区 */
    uint16_t        payload_len;      /* 载荷期望长度 */
    uint16_t        payload_pos;      /* 已读载荷字节数 */
    uint8_t         cur_cmd;          /* 当前帧 CMD */
    uint8_t         cur_flags;        /* 当前帧 FLAGS */
    uint8_t         cur_seq;          /* 当前帧 SEQ */

    /* ---- 发送缓冲区（环形） ---- */
    uint8_t         send_buf[CONN_BUF_SIZE];
    int             send_head;        /* 生产者写入位置 */
    int             send_tail;        /* 消费者读取位置 */
    int             send_len;         /* 待发送字节数 */

    /* ---- 时间轮节点 ---- */
    void           *tw_node;         /* 时间轮任务指针 */

    /* ---- 链表（用于连接管理） ---- */
    struct conn    *next;
} conn_t;

#endif /* GLOBAL_H */
