/**
 * commands.c — 客户端命令实现
 * 负责人：阿杰
 */
#include "commands/commands.h"
#include "../client_protocol/client_protocol.h"
#include "../../common/proto/tlv.h"
#include "../../common/utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

char g_cwd[512]        = "/";
char g_username[64]    = "";
int  g_is_logged_in    = 0;

/* ========================================================================
 * 认证
 * ======================================================================== */

int cmd_register(int sockfd, const char *user, const char *pass)
{
    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_USERNAME, user);
    pos += tlv_write_str(payload + pos, TLV_PASSWORD, pass);
    cli_send_frame(sockfd, CMD_REGISTER, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp) { printf("[ERROR] No response\n"); return -1; }
    if (resp->hdr.cmd != CMD_OK) {
        char msg[256]; tlv_get_str(resp->payload, resp->payload + resp->hdr.payload_len,
                                    TLV_ERROR_MSG, msg, sizeof(msg));
        printf("[ERROR] %s\n", msg);
        frame_free(resp); return -1;
    }
    printf("[OK] Registered. Auto-logging in...\n");
    frame_free(resp);
    return cmd_login(sockfd, user, pass);
}

int cmd_login(int sockfd, const char *user, const char *pass)
{
    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_USERNAME, user);
    pos += tlv_write_str(payload + pos, TLV_PASSWORD, pass);
    cli_send_frame(sockfd, CMD_AUTH, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp) { printf("[ERROR] No response\n"); return -1; }
    if (resp->hdr.cmd != CMD_AUTH_OK) {
        printf("[ERROR] Invalid username or password\n");
        frame_free(resp); return -1;
    }
    strncpy(g_username, user, sizeof(g_username)-1);
    g_is_logged_in = 1;
    printf("[OK] Logged in as %s\n", user);
    frame_free(resp);
    return 0;
}

int cmd_logout(int sockfd)
{
    cli_send_frame(sockfd, CMD_BYE, 0, NULL, 0);
    g_is_logged_in = 0;
    g_username[0]  = '\0';
    printf("[OK] Bye.\n");
    return 0;
}

int cmd_whoami(void)
{
    if (!g_is_logged_in) { printf("Not logged in.\n"); return -1; }
    printf("%s\n", g_username);
    return 0;
}

/* ========================================================================
 * 目录浏览
 * ======================================================================== */

int cmd_ls(int sockfd, const char *path, int detail)
{
    const char *p = path && path[0] ? path : g_cwd;
    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, p);
    cli_send_frame(sockfd, CMD_LS, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp || resp->hdr.cmd != CMD_OK) {
        if (resp) frame_free(resp);
        printf("[ERROR] Failed to list directory\n"); return -1;
    }

    /* 解码 TLV 目录列表 */
    const uint8_t *r = resp->payload, *end = r + resp->hdr.payload_len;
    while (r < end) {
        uint16_t t = r[0], l = (r[1]<<8)|r[2];
        if (t == TLV_ENTRY_START) { r += 3; continue; }
        if (t == TLV_ENTRY_END)   { r += 7; continue; }  /* 跳过终止标记 */
        if (t == TLV_PATH)        { r += 3 + l; continue; }
        if (t != TLV_FILENAME)    { r += 3 + l; continue; }

        char name[256]; memcpy(name, r+3, l); name[l]='\0';
        r += 3 + l;

        uint8_t etype = 0; uint64_t fsize = 0;
        if (r < end && r[0] == TLV_ENTRY_TYPE) { etype=r[3]; r+=4; }
        if (r < end && r[0] == TLV_FILE_SIZE) {
            for (int i = 0; i < 8; i++) fsize = (fsize << 8) | r[3 + i];
            r += 11;
        }

        if (etype == 0x01) printf("  [DIR]  %s/\n", name);
        else if (detail) {
            char human[16];
            if (fsize>1073741824) snprintf(human,16,"%.1f GB",fsize/1073741824.0);
            else if (fsize>1048576) snprintf(human,16,"%.1f MB",fsize/1048576.0);
            else if (fsize>1024) snprintf(human,16,"%.1f KB",fsize/1024.0);
            else snprintf(human,16,"%lu B",(unsigned long)fsize);
            printf("  FILE  %-20s %s\n", name, human);
        } else printf("  %s\n", name);
    }
    frame_free(resp);
    return 0;
}

int cmd_cd(int sockfd, const char *path)
{
    char new_path[512];
    if (path[0]=='/') strcpy(new_path, path);
    else if (strcmp(path,"..")==0) { path_parent(g_cwd, new_path, sizeof(new_path)); }
    else snprintf(new_path, sizeof(new_path), "%s/%s", g_cwd, path);

    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, new_path);
    cli_send_frame(sockfd, CMD_CD, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp || resp->hdr.cmd != CMD_OK) {
        if (resp) frame_free(resp);
        printf("[ERROR] Path not found: %s\n", new_path); return -1;
    }
    strcpy(g_cwd, new_path);
    frame_free(resp);
    return 0;
}

int cmd_pwd(void) { printf("%s\n", g_cwd); return 0; }

int cmd_stat(int sockfd, const char *path)
{
    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, path);
    cli_send_frame(sockfd, CMD_STAT, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp || resp->hdr.cmd != CMD_OK) { if (resp) frame_free(resp); return -1; }
    printf("OK - stub\n");
    frame_free(resp); return 0;
}

/* ========================================================================
 * 文件操作
 * ======================================================================== */

int cmd_mkdir(int sockfd, const char *path)
{
    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, path);
    cli_send_frame(sockfd, CMD_MKDIR, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp || resp->hdr.cmd != CMD_OK) { if (resp) frame_free(resp); printf("[ERROR]\n"); return -1; }
    printf("[OK] Directory created\n");
    frame_free(resp); return 0;
}

int cmd_rm(int sockfd, const char *path)
{
    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, path);
    cli_send_frame(sockfd, CMD_RM, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp || resp->hdr.cmd != CMD_OK) { if (resp) frame_free(resp); printf("[ERROR]\n"); return -1; }
    printf("[OK] Deleted\n");
    frame_free(resp); return 0;
}

int cmd_mv(int sockfd, const char *src, const char *dst)
{
    uint8_t payload[512]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, src);
    pos += tlv_write_str(payload + pos, TLV_FILENAME, dst);
    cli_send_frame(sockfd, CMD_MV, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 5000);
    if (!resp || resp->hdr.cmd != CMD_OK) { if (resp) frame_free(resp); printf("[ERROR]\n"); return -1; }
    printf("[OK] Moved\n");
    frame_free(resp); return 0;
}

/* ========================================================================
 * 上传 (PUT)
 * ======================================================================== */

int cmd_put(int sockfd, const char *local, const char *remote)
{
    struct stat st;
    if (stat(local, &st) < 0) { printf("[ERROR] File not found: %s\n", local); return -1; }

    char hash[65] = {0};
    sha256_file(local, hash);

    /* 首帧：元信息 */
    uint8_t payload[512]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, remote);
    pos += tlv_write_str(payload + pos, TLV_FILENAME, path_basename(local));
    pos += tlv_write_u64(payload + pos, TLV_FILE_SIZE, st.st_size);
    pos += tlv_write(payload + pos, TLV_FILE_HASH, 32, hash);

    cli_send_frame(sockfd, CMD_PUT, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 10000);
    if (!resp || resp->hdr.cmd != CMD_OK) { if (resp) frame_free(resp); return -1; }

    /* 秒传检测 */
    int streaming = resp->hdr.flags & FLAG_STREAMING;
    if (!streaming) {
        printf("[INSTANT] File already exists, skipped upload.\n");
        frame_free(resp); return 0;
    }
    frame_free(resp);

    /* 流式发送文件 */
    int fd = open(local, O_RDONLY);
    if (fd < 0) { printf("[ERROR] Cannot open %s\n", local); return -1; }

    uint8_t chunk[65536]; ssize_t n; uint64_t sent = 0;
    printf("Uploading: ");
    while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
        int p = 0;
        p += tlv_write(payload + p, TLV_CHUNK_DATA, n, chunk);
        uint8_t fl = (sent + n >= (uint64_t)st.st_size) ? 0 : FLAG_STREAMING;
        cli_send_frame(sockfd, CMD_PUT, fl, payload, p);
        sent += n;
        int pct = (int)(sent * 100 / st.st_size);
        printf("\rUploading: [");
        for (int i=0;i<20;i++) printf(i<pct/5?"#":"-");
        printf("] %d%%", pct); fflush(stdout);
    }
    close(fd);

    resp = cli_recv_frame(sockfd, 10000);
    if (resp) {
        uint64_t fid = 0;
        tlv_get_u64(resp->payload, resp->payload + resp->hdr.payload_len, TLV_FILE_ID, &fid);
        printf("\n[OK] File created: id=%lu\n", (unsigned long)fid);
        frame_free(resp);
    }
    return 0;
}

/* ========================================================================
 * 下载 (GET)
 * ======================================================================== */

int cmd_get(int sockfd, const char *remote, const char *local)
{
    /* 自动断点续传检测 */
    uint64_t offset = 0;
    uint8_t cmd = CMD_GET;
    struct stat st;
    if (stat(local, &st) == 0 && st.st_size > 0) {
        offset = st.st_size;
        cmd = CMD_RESUME_GET;
    }

    uint8_t payload[256]; int pos = 0;
    pos += tlv_write_str(payload + pos, TLV_PATH, remote);
    if (offset > 0) pos += tlv_write_u64(payload + pos, TLV_OFFSET, offset);

    cli_send_frame(sockfd, cmd, 0, payload, pos);
    frame_t *resp = cli_recv_frame(sockfd, 10000);
    if (!resp || resp->hdr.cmd != CMD_OK) { if (resp) frame_free(resp); return -1; }

    uint64_t fsize = 0;
    tlv_get_u64(resp->payload, resp->payload + resp->hdr.payload_len, TLV_FILE_SIZE, &fsize);
    frame_free(resp);

    int fd = open(local, O_WRONLY | O_CREAT | (offset > 0 ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) { printf("[ERROR] Cannot create %s\n", local); return -1; }

    uint64_t received = offset;
    printf("Downloading: ");
    while (1) {
        frame_t *data = cli_recv_frame(sockfd, 30000);
        if (!data) break;

        uint16_t clen;
        const uint8_t *chunk = tlv_get_raw(data->payload,
                                            data->payload + data->hdr.payload_len,
                                            TLV_CHUNK_DATA, &clen);
        if (chunk) { write(fd, chunk, clen); received += clen; }

        int pct = fsize ? (int)(received * 100 / fsize) : 100;
        printf("\rDownloading: [");
        for (int i=0;i<20;i++) printf(i<pct/5?"#":"-");
        printf("] %d%%", pct); fflush(stdout);

        int done = !(data->hdr.flags & FLAG_STREAMING);
        frame_free(data);
        if (done) break;
    }
    close(fd);
    printf("\n[OK] Downloaded: %s (%lu bytes)\n", local, (unsigned long)received);
    return 0;
}
