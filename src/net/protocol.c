/**
 * protocol.c — NDP 帧解析状态机实现
 *
 * 5 状态:
 *   FRAME_MAGIC1  (0) — 等待 'N' (0x4E)
 *   FRAME_MAGIC2  (1) — 等待 'D' (0x44)
 *   FRAME_HEADER  (2) — 读剩余 6 字节帧头
 *   FRAME_PAYLOAD (3) — 读 payload_len 字节载荷
 *   FRAME_DONE    (4) — 帧完整
 */
#include "protocol.h"
#include "../base/log/log.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/* 已完成帧链表节点 */
typedef struct frame_node {
    frame_t frame;
    struct frame_node *next;
} frame_node_t;

static frame_node_t *g_completed_head = NULL;
static frame_node_t *g_completed_tail = NULL;

int protocol_feed(conn_t *conn, const uint8_t *data, int len)
{
    int frames_done = 0;
    int offset = 0;

    while (offset < len) {
        uint8_t byte = data[offset++];

        switch (conn->frame_state) {

        case FRAME_MAGIC1:
            if (byte == NDP_MAGIC1) {
                conn->frame_state = FRAME_MAGIC2;
            }
            /* 不是 'N' → 保持 MAGIC1，丢弃该字节 */
            break;

        case FRAME_MAGIC2:
            if (byte == NDP_MAGIC2) {
                conn->frame_state = FRAME_HEADER;
                conn->header_pos = 0;
            } else {
                conn->frame_state = FRAME_MAGIC1;
                /* 如果这个字节恰好是 'N'，重新检查 */
                if (byte == NDP_MAGIC1) {
                    conn->frame_state = FRAME_MAGIC2;
                }
            }
            break;

        case FRAME_HEADER:
            conn->header_buf[conn->header_pos++] = byte;
            if (conn->header_pos >= 6) {
                /* 帧头完整：解析 */
                uint8_t version  = conn->header_buf[0];
                conn->cur_cmd   = conn->header_buf[1];
                conn->cur_flags = conn->header_buf[2];
                conn->cur_seq   = conn->header_buf[3];
                uint16_t be_len;
                memcpy(&be_len, conn->header_buf + 4, 2);
                conn->payload_len = ntohs(be_len);

                (void)version; /* 版本校验可后续添加 */

                /* payload_len 为 uint16_t，最大 65535，不超过 NDP_MAX_PAYLOAD (64KB) */
                if (conn->payload_len == 0) {
                    /* 无载荷 → 直接完成 */
                    conn->frame_state = FRAME_DONE;
                    frames_done++;
                    goto frame_done;
                }

                /* 分配载荷缓冲区 */
                free(conn->payload_buf);
                conn->payload_buf = malloc(conn->payload_len);
                if (!conn->payload_buf) {
                    log_error("protocol: malloc payload failed");
                    conn->frame_state = FRAME_MAGIC1;
                    break;
                }
                conn->payload_pos = 0;
                conn->frame_state = FRAME_PAYLOAD;
            }
            break;

        case FRAME_PAYLOAD:
            conn->payload_buf[conn->payload_pos++] = byte;
            if (conn->payload_pos >= conn->payload_len) {
                conn->frame_state = FRAME_DONE;
                frames_done++;
                goto frame_done;
            }
            break;

        frame_done: /* fall through */
        case FRAME_DONE:
        {
            frame_node_t *node = malloc(sizeof(frame_node_t));
            if (!node) {
                log_error("protocol: malloc frame_node failed");
                free(conn->payload_buf);
                conn->payload_buf = NULL;
                conn->frame_state = FRAME_MAGIC1;
                break;
            }
            memset(node, 0, sizeof(*node));
            node->frame.hdr.magic[0]   = NDP_MAGIC1;
            node->frame.hdr.magic[1]   = NDP_MAGIC2;
            node->frame.hdr.version    = NDP_VERSION;
            node->frame.hdr.cmd        = conn->cur_cmd;
            node->frame.hdr.flags      = conn->cur_flags;
            node->frame.hdr.seq        = conn->cur_seq;
            node->frame.hdr.payload_len = conn->payload_len;
            node->frame.payload         = conn->payload_buf;
            node->frame.payload_capacity = conn->payload_len;

            if (g_completed_tail) {
                g_completed_tail->next = node;
            } else {
                g_completed_head = node;
            }
            g_completed_tail = node;

            conn->frame_state = FRAME_MAGIC1;
            conn->payload_buf = NULL;
            conn->payload_len = 0;
            break;
        }
        }
    }

    return frames_done;
}

frame_t *protocol_get_frame(conn_t *conn)
{
    (void)conn;
    if (!g_completed_head) return NULL;

    frame_node_t *node = g_completed_head;
    g_completed_head = node->next;
    if (!g_completed_head) g_completed_tail = NULL;

    static frame_t s_ret;
    s_ret = node->frame;
    free(node);
    return &s_ret;
}

void frame_free(frame_t *frame)
{
    if (!frame) return;
    free(frame->payload);
    memset(frame, 0, sizeof(*frame));
}
