/**
 * client_protocol.c — 客户端协议栈实现
 *
 * 职责：
 *   - TCP 连接 / 断开
 *   - NDP 帧发送（构造帧头 + TLV 载荷 → send）
 *   - NDP 帧接收（阻塞读 + 超时 + 5 状态机解析）
 */

#include "client_protocol.h"
#include "../../common/proto/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/* ========================================================================
 * 连接管理
 * ======================================================================== */

int cli_connect(const char *host, int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    /* 尝试解析 host 为 IP 或域名 */
    struct hostent *he = gethostbyname(host);
    if (he) {
        memcpy(&addr.sin_addr, he->h_addr, (size_t)he->h_length);
    } else if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid host: %s\n", host);
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void cli_disconnect(int sockfd)
{
    if (sockfd >= 0) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }
}

/* ========================================================================
 * 帧发送
 * ======================================================================== */

int cli_send_frame(int sockfd, uint8_t cmd, uint8_t flags,
                   const uint8_t *payload, uint16_t payload_len)
{
    uint8_t buf[NDP_HEADER_SIZE + NDP_MAX_PAYLOAD];

    /* 构造帧头 */
    buf[0] = NDP_MAGIC1;
    buf[1] = NDP_MAGIC2;
    buf[2] = NDP_VERSION;
    buf[3] = cmd;
    buf[4] = flags;
    buf[5] = 0;  /* SEQ 由客户端维护（简单起见固定为 0） */
    uint16_t be_len = htons(payload_len);
    memcpy(buf + 6, &be_len, 2);

    /* 复制载荷 */
    if (payload && payload_len > 0) {
        memcpy(buf + NDP_HEADER_SIZE, payload, payload_len);
    }

    size_t total = NDP_HEADER_SIZE + payload_len;
    ssize_t sent = send(sockfd, buf, total, 0);
    if (sent != (ssize_t)total) {
        fprintf(stderr, "cli_send_frame: send %zd/%zu failed\n", sent, total);
        return -1;
    }
    return 0;
}

/* ========================================================================
 * 帧接收（阻塞 + 超时 + 状态机解析）
 * ======================================================================== */

/* 内部：等待 sockfd 可读，超时返回 -1，可读返回 0 */
static int wait_readable(int sockfd, int timeout_ms)
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sockfd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
        if (errno == EINTR) return wait_readable(sockfd, timeout_ms);
        return -1;
    }
    if (ret == 0) return -1;  /* 超时 */
    return 0;
}

frame_t *cli_recv_frame(int sockfd, int timeout_ms)
{
    /* 解析状态 */
    enum { ST_MAGIC1, ST_MAGIC2, ST_HEADER, ST_PAYLOAD, ST_DONE } state = ST_MAGIC1;
    uint8_t  header_buf[6];
    int      header_pos = 0;
    uint8_t *payload_buf = NULL;
    uint16_t payload_len = 0;
    uint16_t payload_pos = 0;
    uint8_t  cur_cmd   = 0;
    uint8_t  cur_flags = 0;
    uint8_t  cur_seq   = 0;

    while (1) {
        /* 等待数据到达 */
        if (wait_readable(sockfd, timeout_ms) < 0) {
            free(payload_buf);
            return NULL;
        }

        uint8_t byte;
        ssize_t n = recv(sockfd, &byte, 1, 0);
        if (n <= 0) {
            free(payload_buf);
            return NULL;  /* 连接断开或错误 */
        }

        switch (state) {
        case ST_MAGIC1:
            if (byte == NDP_MAGIC1) state = ST_MAGIC2;
            break;

        case ST_MAGIC2:
            if (byte == NDP_MAGIC2) {
                state = ST_HEADER;
                header_pos = 0;
            } else if (byte == NDP_MAGIC1) {
                state = ST_MAGIC2;  /* 保持 */
            } else {
                state = ST_MAGIC1;
            }
            break;

        case ST_HEADER:
            header_buf[header_pos++] = byte;
            if (header_pos >= 6) {
                /* 解析帧头 */
                /* header_buf[0] = version (ignored) */
                cur_cmd   = header_buf[1];
                cur_flags = header_buf[2];
                cur_seq   = header_buf[3];
                uint16_t be_len;
                memcpy(&be_len, header_buf + 4, 2);
                payload_len = ntohs(be_len);

                if (payload_len > NDP_MAX_PAYLOAD) {
                    free(payload_buf);
                    return NULL;
                }

                if (payload_len == 0) {
                    state = ST_DONE;
                } else {
                    payload_buf = malloc(payload_len);
                    if (!payload_buf) return NULL;
                    payload_pos = 0;
                    state = ST_PAYLOAD;
                }
            }
            break;

        case ST_PAYLOAD:
            payload_buf[payload_pos++] = byte;
            if (payload_pos >= payload_len) {
                state = ST_DONE;
            }
            break;

        case ST_DONE:
            break;
        }

        if (state == ST_DONE) break;
    }

    /* 构造返回帧 */
    frame_t *frame = malloc(sizeof(frame_t));
    if (!frame) {
        free(payload_buf);
        return NULL;
    }

    memset(frame, 0, sizeof(*frame));
    frame->hdr.magic[0]    = NDP_MAGIC1;
    frame->hdr.magic[1]    = NDP_MAGIC2;
    frame->hdr.version     = NDP_VERSION;
    frame->hdr.cmd         = cur_cmd;
    frame->hdr.flags       = cur_flags;
    frame->hdr.seq         = cur_seq;
    frame->hdr.payload_len = payload_len;
    frame->payload         = payload_buf;
    frame->payload_capacity = payload_len;

    return frame;
}

/**
 * frame_free — 释放 cli_recv_frame 返回的帧内存
 */
void frame_free(frame_t *frame)
{
    if (!frame) return;
    free(frame->payload);
    free(frame);
}
