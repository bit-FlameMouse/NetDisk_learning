/**
 * dispatcher.c — 命令分发器实现
 *
 * CMD 码 → handler 函数指针跳转表（14 条目），O(1) 分发。
 */
#include "dispatcher.h"
#include "handler.h"
#include "../base/log/log.h"
#include <string.h>

/* handler 函数指针类型 */
typedef void (*handler_fn)(conn_t *conn, frame_t *frame);

/* 跳转表条目 */
typedef struct {
    uint8_t    cmd;
    handler_fn handler;
    int        need_auth;  /* 是否需要先完成认证（REGISTER/AUTH 除外） */
} cmd_entry_t;

/* O(1) 直接索引跳转表：用 cmd 作为下标（256 条目，未使用的为 NULL） */
static handler_fn g_jump_table[256];

void dispatch(conn_t *conn, frame_t *frame)
{
    uint8_t cmd = frame->hdr.cmd;

    if (g_jump_table[cmd] == NULL) {
        log_warn("dispatcher: unknown cmd 0x%02x from fd=%d", cmd, conn->fd);
        return;
    }

    log_debug("dispatcher: cmd=0x%02x fd=%d user=%d",
              cmd, conn->fd, conn->user_id);
    g_jump_table[cmd](conn, frame);
}

/* 模块初始化：注册所有命令处理器 */
__attribute__((constructor))
static void dispatcher_init(void)
{
    memset(g_jump_table, 0, sizeof(g_jump_table));

    /* 认证命令 */
    g_jump_table[CMD_REGISTER] = handle_register;
    g_jump_table[CMD_AUTH]     = handle_auth;
    g_jump_table[CMD_BYE]      = handle_bye;

    /* 文件浏览 */
    g_jump_table[CMD_LS]   = handle_ls;
    g_jump_table[CMD_CD]   = handle_cd;
    g_jump_table[CMD_STAT] = handle_stat;

    /* 文件操作 */
    g_jump_table[CMD_MKDIR] = handle_mkdir;
    g_jump_table[CMD_RM]    = handle_rm;
    g_jump_table[CMD_MV]    = handle_mv;
    g_jump_table[CMD_PUT]   = handle_put;
    g_jump_table[CMD_GET]   = handle_get;

    /* 高级操作 */
    g_jump_table[CMD_RESUME_PUT] = handle_resume_put;
    g_jump_table[CMD_RESUME_GET] = handle_resume_get;
}
