/**
 * storage.c — 存储引擎
 *
 * 负责人：小赵
 *
 * 物理布局：
 *   {data_dir}/XX/fullsha256hash   — 最终文件（XX = hash 前 2 字符，256 子目录分片）
 *   {tmp_dir}/{upload_id}/chunk_N   — 上传临时分片
 *
 * 上传流程：init → write_chunk × N → finish（合并+SHA256+rename+插库）
 * 下载流程：start → seek_read × N（sendfile 零拷贝由 server.c 处理）
 */

#include "storage.h"
#include "vfs.h"
#include "../data/db.h"
#include "../global.h"
#include "../base/log/log.h"
#include "../../common/utils/utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <mysql/mysql.h>

/* ========================================================================
 * 本地路径工具
 * ======================================================================== */

static int mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

static void tmp_chunk_path(char *buf, size_t sz, uint64_t upload_id, uint32_t seq)
{
    snprintf(buf, sz, "%s/%lu/chunk_%u",
             g_data_dir, (unsigned long)upload_id, seq);
}

static void final_path(char *buf, size_t sz, const char *hash)
{
    snprintf(buf, sz, "%s/%.2s/%s", g_data_dir, hash, hash);
}

/* ========================================================================
 * 秒传检测
 * ======================================================================== */

int storage_check_hash(const char *hash, uint64_t *existing_id)
{
    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;
    *existing_id = db_node_find_by_hash(db, hash);
    db_release(g_db_pool, db);
    return *existing_id > 0 ? 1 : 0;
}

/* ========================================================================
 * 上传
 * ======================================================================== */

int storage_upload_init(uint64_t user_id, uint64_t parent_id, const char *name,
                        uint64_t size, const char *hash, const char *mime,
                        uint64_t *upload_id)
{
    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    int chunks = (int)((size + 4194303) / 4194304);

    *upload_id = db_upload_create(db, user_id, parent_id, name, size, chunks);
    if (*upload_id == 0) { db_release(g_db_pool, db); return -1; }

    char tmp_dir[512];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/%lu", g_data_dir, (unsigned long)*upload_id);
    mkdir(tmp_dir, 0755);

    db_release(g_db_pool, db);
    return 0;
}

int storage_write_chunk(uint64_t upload_id, uint32_t seq,
                        const uint8_t *data, uint32_t len)
{
    char path[512];
    tmp_chunk_path(path, sizeof(path), upload_id, seq);

    /* 确保目录存在 */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%lu", g_data_dir, (unsigned long)upload_id);
    mkdir(dir, 0755);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        log_error("storage_write_chunk: open(%s) failed: %s", path, strerror(errno));
        return -1;
    }

    ssize_t written = write(fd, data, len);
    close(fd);

    if (written != (ssize_t)len) {
        log_error("storage_write_chunk: partial write %zd/%u", written, len);
        return -1;
    }

    return 0;
}

int storage_upload_finish(uint64_t upload_id, vfs_node_t *result)
{
    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    upload_session_t sess;
    if (!db_upload_get(db, upload_id, &sess)) {
        db_release(g_db_pool, db);
        return -1;
    }

    uint64_t user_id   = sess.user_id;
    uint64_t parent_id = sess.parent_id;
    const char *filename = sess.filename;
    uint64_t fsize     = sess.file_size;
    int      chunks    = sess.total_chunks;

    /* ② 合并分片 → 计算 SHA256 */
    char hash_hex[65] = {0};
    char final[512];
    snprintf(final, sizeof(final), "%s/%lu/merged", g_data_dir, (unsigned long)upload_id);

    int out_fd = open(final, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) { db_release(g_db_pool, db); return -1; }

    sha256_ctx_t *ctx = sha256_init();

    for (int i = 0; i < chunks; i++) {
        char cpath[512];
        tmp_chunk_path(cpath, sizeof(cpath), upload_id, i);

        int cfd = open(cpath, O_RDONLY);
        if (cfd < 0) continue;  /* 缺失分片？跳过 */

        uint8_t buf[65536];
        ssize_t n;
        while ((n = read(cfd, buf, sizeof(buf))) > 0) {
            write(out_fd, buf, n);
            sha256_update(ctx, buf, (size_t)n);
        }
        close(cfd);
    }

    close(out_fd);
    sha256_final(ctx, hash_hex);
    sha256_free(ctx);

    /* ③ 移动合并后的文件到哈希目录 */
    char dst[512];
    final_path(dst, sizeof(dst), hash_hex);

    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/%.2s", g_data_dir, hash_hex);
    mkdir(subdir, 0755);

    rename(final, dst);

    /* ④ 二次去重检查（并发场景） */
    uint64_t dup_id;
    if (storage_check_hash(hash_hex, &dup_id) == 1) {
        /* 已有相同 hash → 删除刚合并的文件，复用已有记录 */
        unlink(dst);
    }

    /* ⑤ 插入 nodes 表 */
    vfs_create_file(user_id, parent_id, filename, fsize,
                    hash_hex, mime_by_ext(filename));

    /* ⑥ 清理临时分片 */
    for (int i = 0; i < chunks; i++) {
        char cpath[512];
        tmp_chunk_path(cpath, sizeof(cpath), upload_id, i);
        unlink(cpath);
    }
    char tmp_dir[512];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/%lu", g_data_dir, (unsigned long)upload_id);
    rmdir(tmp_dir);

    /* ⑦ 标记会话完成 */
    db_upload_done(db, upload_id);

    db_release(g_db_pool, db);

    if (result) memset(result, 0, sizeof(*result));

    log_info("Upload finish: upload=%lu hash=%s size=%lu",
             (unsigned long)upload_id, hash_hex, (unsigned long)fsize);
    return 0;
}

/* ========================================================================
 * 下载
 * ======================================================================== */

int storage_download_start(uint64_t file_id, int *out_fd, uint64_t *size, char *hash)
{
    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT content_hash, file_size FROM nodes "
             "WHERE id=%lu AND is_deleted=0",
             (unsigned long)file_id);

    if (mysql_query(db, sql)) {
        log_error("storage_download: query failed: %s", mysql_error(db));
        db_release(g_db_pool, db);
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(db);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        db_release(g_db_pool, db);
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    char hash_hex[65];
    strncpy(hash_hex, row[0], 64);
    uint64_t fsize = strtoull(row[1], NULL, 10);
    mysql_free_result(res);
    db_release(g_db_pool, db);

    /* 构造物理路径 */
    char path[512];
    final_path(path, sizeof(path), hash_hex);

    *out_fd = open(path, O_RDONLY);
    if (*out_fd < 0) {
        log_error("storage_download: open(%s) failed: %s", path, strerror(errno));
        return -1;
    }

    if (size) *size = fsize;
    if (hash) strncpy(hash, hash_hex, 64);

    return 0;
}

int storage_seek_read(int fd, uint64_t offset, uint8_t *buf, uint32_t len,
                      uint32_t *read_bytes)
{
    if (lseek(fd, (off_t)offset, SEEK_SET) < 0) return -1;

    ssize_t n = read(fd, buf, len);
    if (n < 0) return -1;

    *read_bytes = (uint32_t)n;
    return 0;
}
