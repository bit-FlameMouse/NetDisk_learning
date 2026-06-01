/**
 * handler.h — 命令处理器声明
 *
 * 每个 handler 的签名: void handler(conn_t *conn, frame_t *frame)
 * handler 内部：TLV 解码 → 业务逻辑 → TLV 编码响应 → conn_send_frame
 */

#ifndef ROUTE_HANDLER_H
#define ROUTE_HANDLER_H

#include "../global.h"
#include "../../common/proto/types.h"

/* 认证 */
void handle_register(conn_t *conn, frame_t *frame);
void handle_auth(conn_t *conn, frame_t *frame);
void handle_bye(conn_t *conn, frame_t *frame);

/* 文件浏览 */
void handle_ls(conn_t *conn, frame_t *frame);
void handle_cd(conn_t *conn, frame_t *frame);
void handle_stat(conn_t *conn, frame_t *frame);

/* 文件操作 */
void handle_mkdir(conn_t *conn, frame_t *frame);
void handle_rm(conn_t *conn, frame_t *frame);
void handle_mv(conn_t *conn, frame_t *frame);
void handle_put(conn_t *conn, frame_t *frame);
void handle_get(conn_t *conn, frame_t *frame);

/* 高级操作 */
void handle_resume_put(conn_t *conn, frame_t *frame);
void handle_resume_get(conn_t *conn, frame_t *frame);

#endif
