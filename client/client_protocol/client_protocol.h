/**
 * client_protocol.h — 客户端协议栈接口
 *
 * 提供 TCP 连接、帧收发的基础能力。
 * 所有命令实现都通过本接口与服务端通信。
 */

#ifndef CLIENT_PROTOCOL_H
#define CLIENT_PROTOCOL_H

#include "../../common/proto/types.h"
#include <stdint.h>

/* ========================================================================
 * 连接管理
 * ======================================================================== */

/** 连接到服务端，返回 socket fd（失败返回 -1） */
int  cli_connect(const char *host, int port);

/** 断开连接 */
void cli_disconnect(int sockfd);

/* ========================================================================
 * 帧收发
 * ======================================================================== */

/**
 * 发送一帧（阻塞，直到全部发送完成）。
 *
 * @param sockfd       已连接的 socket
 * @param cmd          命令码
 * @param flags        标志位（FLAG_STREAMING 等）
 * @param payload      TLV 编码的载荷
 * @param payload_len  载荷长度
 * @return             0=成功, -1=失败
 */
int cli_send_frame(int sockfd, uint8_t cmd, uint8_t flags,
                   const uint8_t *payload, uint16_t payload_len);

/**
 * 接收一帧（阻塞 + 超时）。
 *
 * @param sockfd       已连接的 socket
 * @param timeout_ms   超时（毫秒），0 = 永不超时
 * @return             堆上分配的 frame_t（调用方负责 frame_free），
 *                     NULL = 超时或连接断开
 */
frame_t *cli_recv_frame(int sockfd, int timeout_ms);

/**
 * 释放 cli_recv_frame 返回的帧内存。
 */
void frame_free(frame_t *frame);

#endif /* CLIENT_PROTOCOL_H */
