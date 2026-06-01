/**
 * dispatcher.h — 命令分发器
 *
 * CMD 码 → handler 函数指针，O(1) 跳转。
 * 包含令牌桶限流中间件。
 */
#ifndef ROUTE_DISPATCHER_H
#define ROUTE_DISPATCHER_H

#include "../global.h"
#include "../../common/proto/types.h"

/**
 * 分发命令到对应的 handler
 * @param conn  客户端连接
 * @param frame 完整帧
 */
void dispatch(conn_t *conn, frame_t *frame);

#endif
