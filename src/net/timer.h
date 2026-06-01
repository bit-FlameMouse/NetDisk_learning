/**
 * timer.h — 时间轮
 *
 * 60 槽位循环时间轮，O(1) 添加/删除/刷新。
 * 管理所有连接的空闲超时 — 60 秒无操作自动踢出。
 */
#ifndef NET_TIMER_H
#define NET_TIMER_H

#include "../global.h"

/* 时间轮任务回调 */
typedef void (*timer_callback_t)(conn_t *conn);

/* ========================================================================
 * 公开接口
 * ======================================================================== */

/** 初始化时间轮 */
int  timer_init(int slot_count);

/** 推进时间轮（每秒调用一次） */
void timer_tick(void);

/** 添加/刷新连接超时（空闲 timeout_sec 秒后踢出） */
int  timer_add(conn_t *conn, int timeout_sec);

/** 移除连接的超时任务 */
void timer_del(conn_t *conn);

/** 销毁时间轮 */
void timer_destroy(void);

#endif
