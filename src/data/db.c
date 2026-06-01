/**
 * db.c — 数据库访问层实现
 *
 * 负责人：老周
 */

#include "db.h"
#include "../base/log/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

/* ========================================================================
 * 连接池
 * ======================================================================== */

struct db_pool {
    MYSQL          **conns;
    int             *in_use;
    int              size;
    int              timeout;
    pthread_mutex_t  lock;
    pthread_cond_t   cond;
};

db_pool_t *db_pool_create(const char *host, int port,
                           const char *user, const char *pass,
                           const char *name, int size, int timeout)
{
    db_pool_t *pool = calloc(1, sizeof(db_pool_t));
    if (!pool) return NULL;

    pool->size    = size;
    pool->timeout = timeout;
    pool->conns   = calloc(size, sizeof(MYSQL *));
    pool->in_use  = calloc(size, sizeof(int));

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);

    int created = 0;
    for (int i = 0; i < size; i++) {
        pool->conns[i] = mysql_init(NULL);
        if (!pool->conns[i]) continue;

        if (mysql_real_connect(pool->conns[i], host, user, pass, name,
                               port, NULL, 0)) {
            created++;
        } else {
            log_warn("db_pool: conn %d failed: %s", i, mysql_error(pool->conns[i]));
            mysql_close(pool->conns[i]);
            pool->conns[i] = NULL;
        }
    }

    log_info("DB pool: %d/%d connections", created, size);
    return pool;
}

MYSQL *db_acquire(db_pool_t *pool)
{
    if (!pool) return NULL;

    pthread_mutex_lock(&pool->lock);

    while (1) {
        for (int i = 0; i < pool->size; i++) {
            if (pool->conns[i] && !pool->in_use[i]) {
                pool->in_use[i] = 1;
                pthread_mutex_unlock(&pool->lock);
                mysql_ping(pool->conns[i]);
                return pool->conns[i];
            }
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += pool->timeout;
        if (pthread_cond_timedwait(&pool->cond, &pool->lock, &ts) == ETIMEDOUT) {
            log_error("db_acquire: timeout %ds", pool->timeout);
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }
    }
}

void db_release(db_pool_t *pool, MYSQL *conn)
{
    if (!pool || !conn) return;
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < pool->size; i++) {
        if (pool->conns[i] == conn) {
            pool->in_use[i] = 0;
            pthread_cond_signal(&pool->cond);
            break;
        }
    }
    pthread_mutex_unlock(&pool->lock);
}

void db_pool_destroy(db_pool_t *pool)
{
    if (!pool) return;
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < pool->size; i++)
        if (pool->conns[i]) mysql_close(pool->conns[i]);
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    free(pool->conns);
    free(pool->in_use);
    free(pool);
}

/* ========================================================================
 * 内部工具
 * ======================================================================== */

static int exec(MYSQL *db, const char *sql)
{
    if (mysql_query(db, sql)) {
        log_error("db: %s", mysql_error(db));
        return -1;
    }
    return 0;
}

static MYSQL_RES *query(MYSQL *db, const char *sql)
{
    if (mysql_query(db, sql)) {
        log_error("db: %s", mysql_error(db));
        return NULL;
    }
    return mysql_store_result(db);
}

static void esc(MYSQL *db, char *out, size_t sz, const char *in)
{
    mysql_real_escape_string(db, out, in, (unsigned long)strlen(in));
    (void)sz;
}

/* ========================================================================
 * 用户
 * ======================================================================== */

uint64_t db_user_create(MYSQL *db, const char *username,
                         const char *pwd_hash, const char *salt)
{
    char u[128], h[128], s[128];
    esc(db, u, sizeof(u), username);
    esc(db, h, sizeof(h), pwd_hash);
    esc(db, s, sizeof(s), salt);

    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO users (username, password, salt) VALUES ('%s','%s','%s')",
             u, h, s);
    if (exec(db, sql) < 0) return 0;
    return mysql_insert_id(db);
}

int db_user_find(MYSQL *db, const char *username, user_t *out)
{
    char u[128]; esc(db, u, sizeof(u), username);

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, username, password, salt, quota_bytes, used_bytes, "
             "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(COALESCE(last_login, created_at)) "
             "FROM users WHERE username='%s'", u);

    MYSQL_RES *res = query(db, sql);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    memset(out, 0, sizeof(*out));
    out->id          = strtoull(row[0], NULL, 10);
    strncpy(out->username,        row[1], 63);
    strncpy(out->password_hash,   row[2], 128);
    strncpy(out->salt,            row[3], 64);
    out->quota_bytes = strtoull(row[4], NULL, 10);
    out->used_bytes  = strtoull(row[5], NULL, 10);
    out->created_at  = (time_t)atol(row[6]);
    out->last_login  = (time_t)atol(row[7]);

    mysql_free_result(res);
    return 1;
}

int db_user_find_by_id(MYSQL *db, uint64_t id, user_t *out)
{
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, username, password, salt, quota_bytes, used_bytes, "
             "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(COALESCE(last_login, created_at)) "
             "FROM users WHERE id=%lu", (unsigned long)id);

    MYSQL_RES *res = query(db, sql);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return 0;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    memset(out, 0, sizeof(*out));
    out->id          = id;
    strncpy(out->username,        row[1], 63);
    strncpy(out->password_hash,   row[2], 128);
    strncpy(out->salt,            row[3], 64);
    out->quota_bytes = strtoull(row[4], NULL, 10);
    out->used_bytes  = strtoull(row[5], NULL, 10);
    out->created_at  = (time_t)atol(row[6]);
    out->last_login  = (time_t)atol(row[7]);
    mysql_free_result(res);
    return 1;
}

int db_user_exists(MYSQL *db, const char *username)
{
    char u[128]; esc(db, u, sizeof(u), username);
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT 1 FROM users WHERE username='%s'", u);
    MYSQL_RES *res = query(db, sql);
    if (!res) return 0;
    int exists = mysql_num_rows(res) > 0;
    mysql_free_result(res);
    return exists;
}

void db_user_touch_login(MYSQL *db, uint64_t user_id)
{
    char sql[128];
    snprintf(sql, sizeof(sql),
             "UPDATE users SET last_login=NOW() WHERE id=%lu",
             (unsigned long)user_id);
    exec(db, sql);
}

/* ========================================================================
 * 节点
 * ======================================================================== */

uint64_t db_node_root(MYSQL *db, uint64_t user_id)
{
    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT id FROM nodes WHERE user_id=%lu "
             "AND parent_id IS NULL AND is_deleted=0",
             (unsigned long)user_id);
    MYSQL_RES *res = query(db, sql);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return 0;
    }
    uint64_t id = strtoull(mysql_fetch_row(res)[0], NULL, 10);
    mysql_free_result(res);
    return id;
}

uint64_t db_node_find(MYSQL *db, uint64_t user_id,
                       uint64_t parent_id, const char *name)
{
    char n[256]; esc(db, n, sizeof(n), name);
    char sql[512];
    if (parent_id == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT id FROM nodes WHERE user_id=%lu "
                 "AND parent_id IS NULL AND name='%s' AND is_deleted=0",
                 (unsigned long)user_id, n);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT id FROM nodes WHERE user_id=%lu "
                 "AND parent_id=%lu AND name='%s' AND is_deleted=0",
                 (unsigned long)user_id, (unsigned long)parent_id, n);
    }

    MYSQL_RES *res = query(db, sql);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return 0;
    }
    uint64_t id = strtoull(mysql_fetch_row(res)[0], NULL, 10);
    mysql_free_result(res);
    return id;
}

uint64_t db_node_create_file(MYSQL *db, uint64_t user_id,
                              uint64_t parent_id, const char *name,
                              uint64_t size, const char *hash,
                              const char *mime)
{
    char n[256], h[128], m[256];
    esc(db, n, sizeof(n), name);
    esc(db, h, sizeof(h), hash);
    esc(db, m, sizeof(m), mime);

    char sql[768];
    if (parent_id == 0) {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO nodes (user_id, parent_id, name, node_type, "
                 "file_size, content_hash, mime_type) "
                 "VALUES (%lu, NULL, '%s', 'file', %lu, '%s', '%s')",
                 (unsigned long)user_id, n, (unsigned long)size, h, m);
    } else {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO nodes (user_id, parent_id, name, node_type, "
                 "file_size, content_hash, mime_type) "
                 "VALUES (%lu, %lu, '%s', 'file', %lu, '%s', '%s')",
                 (unsigned long)user_id, (unsigned long)parent_id,
                 n, (unsigned long)size, h, m);
    }
    if (exec(db, sql) < 0) return 0;
    return mysql_insert_id(db);
}

uint64_t db_node_create_dir(MYSQL *db, uint64_t user_id,
                             uint64_t parent_id, const char *name)
{
    char n[256]; esc(db, n, sizeof(n), name);
    char sql[512];
    if (parent_id == 0) {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO nodes (user_id, parent_id, name, node_type) "
                 "VALUES (%lu, NULL, '%s', 'directory')",
                 (unsigned long)user_id, n);
    } else {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO nodes (user_id, parent_id, name, node_type) "
                 "VALUES (%lu, %lu, '%s', 'directory')",
                 (unsigned long)user_id, (unsigned long)parent_id, n);
    }
    if (exec(db, sql) < 0) return 0;
    return mysql_insert_id(db);
}

int db_node_list(MYSQL *db, uint64_t user_id, uint64_t parent_id,
                 vfs_node_t **entries, int *count)
{
    char sql[512];
    if (parent_id == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT id, name, node_type, file_size, "
                 "COALESCE(content_hash,''), COALESCE(mime_type,''), "
                 "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
                 "FROM nodes WHERE user_id=%lu AND parent_id IS NULL "
                 "AND is_deleted=0 ORDER BY node_type ASC, name ASC",
                 (unsigned long)user_id);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT id, name, node_type, file_size, "
                 "COALESCE(content_hash,''), COALESCE(mime_type,''), "
                 "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
                 "FROM nodes WHERE user_id=%lu AND parent_id=%lu "
                 "AND is_deleted=0 ORDER BY node_type ASC, name ASC",
                 (unsigned long)user_id, (unsigned long)parent_id);
    }

    MYSQL_RES *res = query(db, sql);
    if (!res) return -1;

    int rows = (int)mysql_num_rows(res);
    *entries = calloc(rows, sizeof(vfs_node_t));
    *count   = rows;

    for (int i = 0; i < rows; i++) {
        MYSQL_ROW row = mysql_fetch_row(res);
        vfs_node_t *e = &(*entries)[i];
        e->id        = strtoull(row[0], NULL, 10);
        strncpy(e->name, row[1], 255);
        e->node_type = (uint8_t)(strcmp(row[2], "directory") == 0 ? 0 : 1);
        e->file_size = strtoull(row[3], NULL, 10);
        strncpy(e->content_hash, row[4], 64);
        strncpy(e->mime_type,   row[5], 127);
        e->created_at = (time_t)atol(row[6]);
        e->updated_at = (time_t)atol(row[7]);
    }

    mysql_free_result(res);
    return 0;
}

int db_node_delete(MYSQL *db, uint64_t user_id, uint64_t node_id)
{
    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE nodes SET is_deleted=1 WHERE id=%lu AND user_id=%lu",
             (unsigned long)node_id, (unsigned long)user_id);
    return exec(db, sql);
}

int db_node_move(MYSQL *db, uint64_t user_id, uint64_t node_id,
                 uint64_t new_parent, const char *new_name)
{
    char n[256]; esc(db, n, sizeof(n), new_name);
    char sql[512];
    if (new_parent == 0) {
        snprintf(sql, sizeof(sql),
                 "UPDATE nodes SET parent_id=NULL, name='%s' "
                 "WHERE id=%lu AND user_id=%lu",
                 n, (unsigned long)node_id, (unsigned long)user_id);
    } else {
        snprintf(sql, sizeof(sql),
                 "UPDATE nodes SET parent_id=%lu, name='%s' "
                 "WHERE id=%lu AND user_id=%lu",
                 (unsigned long)new_parent, n,
                 (unsigned long)node_id, (unsigned long)user_id);
    }
    return exec(db, sql);
}

uint64_t db_node_find_by_hash(MYSQL *db, const char *hash)
{
    char h[128]; esc(db, h, sizeof(h), hash);
    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT id FROM nodes WHERE content_hash='%s' AND is_deleted=0 LIMIT 1",
             h);
    MYSQL_RES *res = query(db, sql);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return 0;
    }
    uint64_t id = strtoull(mysql_fetch_row(res)[0], NULL, 10);
    mysql_free_result(res);
    return id;
}

/* ========================================================================
 * 上传会话
 * ======================================================================== */

uint64_t db_upload_create(MYSQL *db, uint64_t user_id,
                           uint64_t parent_id, const char *filename,
                           uint64_t total_size, int total_chunks)
{
    char fn[256]; esc(db, fn, sizeof(fn), filename);
    char sql[768];
    if (parent_id == 0) {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO upload_sessions "
                 "(user_id, parent_id, filename, total_size, total_chunks, chunk_bitmap) "
                 "VALUES (%lu, NULL, '%s', %lu, %d, REPEAT('\\0', 512))",
                 (unsigned long)user_id, fn,
                 (unsigned long)total_size, total_chunks);
    } else {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO upload_sessions "
                 "(user_id, parent_id, filename, total_size, total_chunks, chunk_bitmap) "
                 "VALUES (%lu, %lu, '%s', %lu, %d, REPEAT('\\0', 512))",
                 (unsigned long)user_id, (unsigned long)parent_id, fn,
                 (unsigned long)total_size, total_chunks);
    }
    if (exec(db, sql) < 0) return 0;
    return mysql_insert_id(db);
}

int db_upload_get(MYSQL *db, uint64_t upload_id, upload_session_t *out)
{
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, user_id, parent_id, filename, total_size, "
             "total_chunks, completed_count, "
             "UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
             "FROM upload_sessions WHERE id=%lu",
             (unsigned long)upload_id);

    MYSQL_RES *res = query(db, sql);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    memset(out, 0, sizeof(*out));
    out->upload_id      = strtoull(row[0], NULL, 10);
    out->user_id        = strtoull(row[1], NULL, 10);
    out->parent_id      = strtoull(row[2], NULL, 10);
    strncpy(out->filename,      row[3], 255);
    out->file_size      = strtoull(row[4], NULL, 10);
    out->total_chunks   = atoi(row[5]);
    out->received_chunks = atoi(row[6]);
    out->created_at     = (time_t)atol(row[7]);
    out->expires_at     = (time_t)atol(row[8]) + 86400;

    mysql_free_result(res);
    return 1;
}

void db_upload_done(MYSQL *db, uint64_t upload_id)
{
    char sql[128];
    snprintf(sql, sizeof(sql),
             "UPDATE upload_sessions SET status='DONE' WHERE id=%lu",
             (unsigned long)upload_id);
    exec(db, sql);
}
