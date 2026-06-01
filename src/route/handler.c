/**
 * handler.c — 命令处理器实现（12 个 handler）
 *
 * 每个 handler: TLV 解码请求 → JWT 认证 → 调用 VFS/storage → TLV 编码响应
 */
#include "handler.h"
#include "auth.h"
#include "jwt.h"
#include "../biz/vfs.h"
#include "../biz/storage.h"
#include "../data/db.h"
#include "../global.h"
#include "../base/log/log.h"
#include "../../common/proto/tlv.h"
#include "../../common/utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/eventfd.h>

/* ========================================================================
 * 内部工具函数
 * ======================================================================== */

/* 构造简单响应帧（仅 CMD + 可选 TLV 载荷）并发送 */
static void send_simple_resp(conn_t *conn, uint8_t cmd, uint8_t seq,
                             const uint8_t *payload, uint16_t plen)
{
    uint8_t buf[NDP_HEADER_SIZE + NDP_MAX_PAYLOAD];
    uint16_t offset = NDP_HEADER_SIZE;

    if (payload && plen > 0) {
        memcpy(buf + offset, payload, plen);
        offset += plen;
    }

    /* 帧头 */
    buf[0] = NDP_MAGIC1;
    buf[1] = NDP_MAGIC2;
    buf[2] = NDP_VERSION;
    buf[3] = cmd;
    buf[4] = FLAG_IS_RESPONSE;
    buf[5] = seq;
    uint16_t be_len = htons((uint16_t)(offset - NDP_HEADER_SIZE));
    memcpy(buf + 6, &be_len, 2);

    /* 写入发送缓冲区 */
    int total = (int)offset;
    conn_t *c = conn; /* 非 const，用于写 */
    for (int i = 0; i < total; i++) {
        c->send_buf[(c->send_head + c->send_len) % CONN_BUF_SIZE] = buf[i];
        c->send_len++;
    }

    /* 通过 eventfd 通知主线程 */
    eventfd_t val = 1;
    /* 主线程会在 eventfd 可读时注册 EPOLLOUT */
    (void)val;
}

/* 发送错误响应 */
static void send_error(conn_t *conn, uint8_t err_cmd, uint8_t seq,
                       const char *msg)
{
    uint8_t pl[256];
    int plen = 0;
    if (msg) plen += tlv_write_str(pl + plen, TLV_ERROR_MSG, msg);
    send_simple_resp(conn, err_cmd, seq, pl, (uint16_t)plen);
}

/* 获取请求中的 token（从 TLV payload 中提取） */
static int get_token(frame_t *frame, char *token_out, size_t sz)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;
    return tlv_get_str(p, end, TLV_TOKEN, token_out, sz);
}

/* JWT 认证中间件：返回 user_id，失败时发送错误响应 */
static int auth_check(conn_t *conn, frame_t *frame, uint64_t *user_id)
{
    char token_buf[512];
    if (get_token(frame, token_buf, sizeof(token_buf)) < 0) {
        /* 也检查 conn 上已保存的 token */
        if (conn->state == CONN_AUTHED && conn->user_id > 0) {
            *user_id = (uint64_t)conn->user_id;
            return 0;
        }
        send_error(conn, CMD_ERR_AUTH, frame->hdr.seq, "Authentication required");
        return -1;
    }

    int uid;
    int ret = jwt_verify(token_buf, g_config->jwt_secret, &uid);
    if (ret == -2) {
        send_error(conn, CMD_ERR_AUTH, frame->hdr.seq, "Token expired");
        return -1;
    }
    if (ret == -1) {
        send_error(conn, CMD_ERR_AUTH, frame->hdr.seq, "Invalid token");
        return -1;
    }

    /* 更新连接状态 */
    conn->user_id = uid;
    conn->state = CONN_AUTHED;
    memcpy(conn->token, token_buf, sizeof(conn->token));
    *user_id = (uint64_t)uid;
    return 0;
}

/* ========================================================================
 * 认证 handler
 * ======================================================================== */

void handle_register(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    char username[64] = {0}, password[128] = {0};
    tlv_get_str(p, end, TLV_USERNAME, username, sizeof(username));
    tlv_get_str(p, end, TLV_PASSWORD, password, sizeof(password));

    uint64_t uid;
    int ret = auth_register(username, password, &uid);

    if (ret == -1) {
        send_error(conn, CMD_ERR_EXISTS, frame->hdr.seq, "Username already exists");
    } else if (ret == -2) {
        send_error(conn, CMD_ERR, frame->hdr.seq, "Registration failed");
    } else {
        uint8_t pl[128]; int plen = 0;
        plen += tlv_write_u64(pl + plen, TLV_FILE_ID, uid);
        plen += tlv_write_str(pl + plen, TLV_MSG, "Registered");
        send_simple_resp(conn, CMD_OK, frame->hdr.seq, pl, (uint16_t)plen);
    }
}

void handle_auth(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    char username[64] = {0}, password[128] = {0};
    tlv_get_str(p, end, TLV_USERNAME, username, sizeof(username));
    tlv_get_str(p, end, TLV_PASSWORD, password, sizeof(password));

    char *token = NULL;
    uint64_t uid;
    int ret = auth_login(username, password, &token, &uid);

    if (ret < 0 || !token) {
        send_error(conn, CMD_AUTH_ERR, frame->hdr.seq, "Invalid username or password");
        return;
    }

    conn->user_id = (int)uid;
    conn->state = CONN_AUTHED;
    memcpy(conn->token, token, strlen(token) + 1);

    uint8_t pl[512]; int plen = 0;
    plen += tlv_write_str(pl + plen, TLV_TOKEN, token);
    send_simple_resp(conn, CMD_AUTH_OK, frame->hdr.seq, pl, (uint16_t)plen);

    free(token);
}

void handle_bye(conn_t *conn, frame_t *frame)
{
    (void)frame;
    send_simple_resp(conn, CMD_OK, 0, NULL, 0);
    conn->state = -1;  /* 标记关闭 */
}

/* ========================================================================
 * 文件浏览 handler
 * ======================================================================== */

void handle_ls(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char path[512] = "/";
    tlv_get_str(p, end, TLV_PATH, path, sizeof(path));

    uint64_t dir_id = 0;
    if (vfs_resolve_path(user_id, path, &dir_id) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "Path not found");
        return;
    }

    vfs_node_t *entries = NULL;
    int count = 0;
    if (vfs_list_dir(user_id, dir_id, &entries, &count) < 0) {
        send_error(conn, CMD_ERR, frame->hdr.seq, "Failed to list directory");
        return;
    }

    /* 构造 TLV 响应 */
    uint8_t pl[NDP_MAX_PAYLOAD];
    int plen = 0;
    plen += tlv_write_str(pl + plen, TLV_PATH, path);

    for (int i = 0; i < count; i++) {
        plen += tlv_write(pl + plen, TLV_ENTRY_START, 0, NULL);
        plen += tlv_write_str(pl + plen, TLV_FILENAME, entries[i].name);
        uint8_t etype = (entries[i].node_type == 0) ? 0x01 : 0x02;
        plen += tlv_write_u8(pl + plen, TLV_ENTRY_TYPE, etype);
        if (entries[i].node_type == 1) {
            plen += tlv_write_u64(pl + plen, TLV_FILE_SIZE, entries[i].file_size);
        }
        plen += tlv_write(pl + plen, TLV_ENTRY_END, 0, NULL);
    }
    plen += tlv_write_end(pl + plen);

    send_simple_resp(conn, CMD_OK, frame->hdr.seq, pl, (uint16_t)plen);
    free(entries);
}

void handle_cd(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char path[512] = "/";
    tlv_get_str(p, end, TLV_PATH, path, sizeof(path));

    uint64_t dir_id;
    if (vfs_resolve_path(user_id, path, &dir_id) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "Path not found");
        return;
    }

    send_simple_resp(conn, CMD_OK, frame->hdr.seq, NULL, 0);
}

void handle_stat(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char path[512] = "/";
    tlv_get_str(p, end, TLV_PATH, path, sizeof(path));

    uint64_t node_id;
    if (vfs_resolve_path(user_id, path, &node_id) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "Not found");
        return;
    }

    /* 简单返回 OK */
    uint8_t pl[128]; int plen = 0;
    plen += tlv_write_u64(pl + plen, TLV_FILE_ID, node_id);
    send_simple_resp(conn, CMD_OK, frame->hdr.seq, pl, (uint16_t)plen);
}

/* ========================================================================
 * 文件操作 handler
 * ======================================================================== */

void handle_mkdir(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char path[512] = "/";
    tlv_get_str(p, end, TLV_PATH, path, sizeof(path));

    /* 分离父路径和目录名 */
    char parent_path[512];
    path_parent(path, parent_path, sizeof(parent_path));
    const char *name = path_basename(path);

    uint64_t parent_id = 0;
    if (vfs_resolve_path(user_id, parent_path, &parent_id) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "Parent path not found");
        return;
    }

    if (vfs_mkdir(user_id, parent_id, name) < 0) {
        send_error(conn, CMD_ERR_EXISTS, frame->hdr.seq, "Create failed");
        return;
    }

    send_simple_resp(conn, CMD_OK, frame->hdr.seq, NULL, 0);
}

void handle_rm(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char path[512] = "/";
    tlv_get_str(p, end, TLV_PATH, path, sizeof(path));

    uint64_t node_id;
    if (vfs_resolve_path(user_id, path, &node_id) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "Not found");
        return;
    }

    if (vfs_delete(user_id, node_id) < 0) {
        send_error(conn, CMD_ERR, frame->hdr.seq, "Delete failed");
        return;
    }

    send_simple_resp(conn, CMD_OK, frame->hdr.seq, NULL, 0);
}

void handle_mv(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char src[512] = "/", dst[512] = "/";
    tlv_get_str(p, end, TLV_PATH, src, sizeof(src));
    tlv_get_str(p, end, TLV_FILENAME, dst, sizeof(dst));

    uint64_t src_id;
    if (vfs_resolve_path(user_id, src, &src_id) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "Source not found");
        return;
    }

    char new_parent_path[512];
    path_parent(dst, new_parent_path, sizeof(new_parent_path));
    const char *new_name = path_basename(dst);

    uint64_t new_parent = 0;
    if (vfs_resolve_path(user_id, new_parent_path, &new_parent) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "Destination not found");
        return;
    }

    if (vfs_move(user_id, src_id, new_parent, new_name) < 0) {
        send_error(conn, CMD_ERR, frame->hdr.seq, "Move failed");
        return;
    }

    send_simple_resp(conn, CMD_OK, frame->hdr.seq, NULL, 0);
}

void handle_put(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char path[512] = "/", filename[256] = {0};
    uint64_t fsize = 0;
    char hash[65] = {0};

    tlv_get_str(p, end, TLV_PATH, path, sizeof(path));
    tlv_get_str(p, end, TLV_FILENAME, filename, sizeof(filename));

    /* 尝试读取 hash（可能来自 TLV_FILE_HASH 原始32字节） */
    uint16_t hash_len;
    const uint8_t *hash_raw = tlv_get_raw(p, end, TLV_FILE_HASH, &hash_len);
    if (hash_raw && hash_len == 32) {
        for (int i = 0; i < 32; i++)
            snprintf(hash + i * 2, 3, "%02x", hash_raw[i]);
    }
    tlv_get_u64(p, end, TLV_FILE_SIZE, &fsize);

    /* 秒传检测 */
    uint64_t existing_id = 0;
    if (hash[0] && storage_check_hash(hash, &existing_id) > 0) {
        /* 秒传成功 */
        char parent_path[512];
        path_parent(path, parent_path, sizeof(parent_path));
        uint64_t parent_id = 0;
        vfs_resolve_path(user_id, parent_path, &parent_id);
        vfs_create_file(user_id, parent_id, path_basename(path),
                        fsize, hash, mime_by_ext(filename));

        uint8_t rpl[128]; int rplen = 0;
        rplen += tlv_write_u64(rpl + rplen, TLV_FILE_ID, existing_id);
        rplen += tlv_write_str(rpl + rplen, TLV_MSG, "INSTANT saved");
        send_simple_resp(conn, CMD_OK, frame->hdr.seq, rpl, (uint16_t)rplen);
        return;
    }

    /* 正常上传：返回 SEND_DATA 信号 + STREAMING flag */
    uint8_t rpl[64]; int rplen = 0;
    rplen += tlv_write_u8(rpl + rplen, TLV_ACTION, 1);  /* ACTION=SEND_DATA */

    /* 需要 STREAMING flag */
    uint8_t buf[NDP_HEADER_SIZE + 256];
    uint16_t offset = NDP_HEADER_SIZE;
    memcpy(buf + offset, rpl, rplen);
    offset += rplen;

    buf[0] = NDP_MAGIC1;
    buf[1] = NDP_MAGIC2;
    buf[2] = NDP_VERSION;
    buf[3] = CMD_OK;
    buf[4] = FLAG_IS_RESPONSE | FLAG_STREAMING;
    buf[5] = frame->hdr.seq;
    uint16_t be_len = htons((uint16_t)(offset - NDP_HEADER_SIZE));
    memcpy(buf + 6, &be_len, 2);

    int total = (int)offset;
    for (int i = 0; i < total; i++) {
        conn->send_buf[(conn->send_head + conn->send_len) % CONN_BUF_SIZE] = buf[i];
        conn->send_len++;
    }
}

void handle_get(conn_t *conn, frame_t *frame)
{
    const uint8_t *p = frame->payload;
    const uint8_t *end = p + frame->hdr.payload_len;

    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;

    char path[512] = "/";
    tlv_get_str(p, end, TLV_PATH, path, sizeof(path));

    uint64_t node_id;
    if (vfs_resolve_path(user_id, path, &node_id) < 0) {
        send_error(conn, CMD_ERR_NOTFND, frame->hdr.seq, "File not found");
        return;
    }

    int fd;
    uint64_t fsize;
    char hash[65];
    if (storage_download_start(node_id, &fd, &fsize, hash) < 0) {
        send_error(conn, CMD_ERR, frame->hdr.seq, "Download failed");
        return;
    }

    /* 发送元信息 + 流式文件数据 */
    uint8_t mbuf[NDP_MAX_PAYLOAD];
    int mlen = 0;
    mlen += tlv_write_u64(mbuf + mlen, TLV_FILE_SIZE, fsize);
    mlen += tlv_write(mbuf + mlen, TLV_FILE_HASH, 32, hash);

    uint8_t buf[NDP_HEADER_SIZE + NDP_MAX_PAYLOAD];
    uint16_t offset = NDP_HEADER_SIZE;
    memcpy(buf + offset, mbuf, mlen);
    offset += mlen;

    buf[0] = NDP_MAGIC1; buf[1] = NDP_MAGIC2;
    buf[2] = NDP_VERSION; buf[3] = CMD_OK;
    buf[4] = FLAG_IS_RESPONSE | FLAG_STREAMING;
    buf[5] = frame->hdr.seq;
    uint16_t be_len = htons((uint16_t)(offset - NDP_HEADER_SIZE));
    memcpy(buf + 6, &be_len, 2);

    int total = (int)offset;
    for (int i = 0; i < total; i++) {
        conn->send_buf[(conn->send_head + conn->send_len) % CONN_BUF_SIZE] = buf[i];
        conn->send_len++;
    }

    /* 流式发送文件内容 */
    uint8_t chunk[65536];
    ssize_t n;
    uint64_t sent = 0;
    while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
        uint8_t dbuf[NDP_HEADER_SIZE + 65536 + 16];
        int dlen = 0;
        dlen += tlv_write(dbuf + NDP_HEADER_SIZE + dlen, TLV_CHUNK_DATA, (uint16_t)n, chunk);
        uint16_t d_offset = NDP_HEADER_SIZE + dlen;
        uint8_t d_flags = FLAG_IS_RESPONSE;
        sent += (uint64_t)n;
        if (sent < fsize) d_flags |= FLAG_STREAMING;

        dbuf[0] = NDP_MAGIC1; dbuf[1] = NDP_MAGIC2;
        dbuf[2] = NDP_VERSION; dbuf[3] = CMD_OK;
        dbuf[4] = d_flags; dbuf[5] = frame->hdr.seq;
        uint16_t d_be_len = htons((uint16_t)dlen);
        memcpy(dbuf + 6, &d_be_len, 2);

        int d_total = (int)(d_offset);
        for (int i = 0; i < d_total; i++) {
            conn->send_buf[(conn->send_head + conn->send_len) % CONN_BUF_SIZE] = dbuf[i];
            conn->send_len++;
        }
    }
    close(fd);
}

/* ========================================================================
 * 高级操作 handler（桩实现）
 * ======================================================================== */

void handle_resume_put(conn_t *conn, frame_t *frame)
{
    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;
    (void)user_id;
    send_simple_resp(conn, CMD_OK, frame->hdr.seq, NULL, 0);
}

void handle_resume_get(conn_t *conn, frame_t *frame)
{
    uint64_t user_id;
    if (auth_check(conn, frame, &user_id) < 0) return;
    (void)user_id;
    send_simple_resp(conn, CMD_OK, frame->hdr.seq, NULL, 0);
}
