/**
 * protocol.h — NDP 帧解析状态机
 *
 * 5 状态：MAGIC1 → MAGIC2 → HEADER → PAYLOAD → DONE
 * 每连接独立状态，非阻塞推进。
 */
#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include "../global.h"
#include "../../common/proto/types.h"
#include <stdint.h>

/**
 * 向连接读缓冲区推送数据，推进帧解析状态机。
 *
 * @param conn  客户端连接
 * @param data  新到达的数据
 * @param len   数据长度
 * @return      解析完成的帧数（0 = 未完成，>0 = 完成了若干帧）
 *              完成的帧通过回调或返回给调用方处理
 */
int protocol_feed(conn_t *conn, const uint8_t *data, int len);

/**
 * 获取最近解析完成的帧（调用 protocol_feed 后检查）
 * 帧的 payload 在堆上分配，调用方负责 frame_free()。
 */
frame_t *protocol_get_frame(conn_t *conn);

/**
 * 释放帧占用的内存
 */
void frame_free(frame_t *frame);

#endif
