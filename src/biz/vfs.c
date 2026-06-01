/**
 * vfs.c — 虚拟文件系统实现
 *
 * 路径逐级解析算法：
 *   1. 取用户根节点
 *   2. 按 '/' 分割路径
 *   3. 逐级 SELECT ... WHERE parent_id=? AND name=?
 *   4. 任意一级查不到 → 返回 ERR_NOT_FOUND
 */
#include "vfs.h"
#include "../data/db.h"
#include "../global.h"
#include "../base/log/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int vfs_resolve_path(uint64_t user_id, const char *path, uint64_t *node_id)
{
    if (!path || !node_id) return -1;

    /* 根路径 */
    if (strcmp(path, "/") == 0 || path[0] == '\0') {
        *node_id = 0;  /* 0 = 虚拟根 */
        return 0;
    }

    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    /* 从根节点开始 */
    uint64_t cur_parent = 0;

    /* 分割路径 */
    char path_copy[512];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(path_copy, "/", &saveptr);

    while (token) {
        uint64_t found = db_node_find(db, user_id, cur_parent, token);
        if (found == 0) {
            db_release(g_db_pool, db);
            return -1;  /* 路径中某一级不存在 */
        }
        cur_parent = found;
        token = strtok_r(NULL, "/", &saveptr);
    }

    db_release(g_db_pool, db);
    *node_id = cur_parent;
    return 0;
}

int vfs_mkdir(uint64_t user_id, uint64_t parent_id, const char *name)
{
    if (!name) return -1;

    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    uint64_t id = db_node_create_dir(db, user_id, parent_id, name);
    db_release(g_db_pool, db);

    if (id == 0) return -1;
    log_debug("vfs_mkdir: user=%lu parent=%lu name=%s -> id=%lu",
              (unsigned long)user_id, (unsigned long)parent_id, name, (unsigned long)id);
    return 0;
}

int vfs_list_dir(uint64_t user_id, uint64_t dir_id,
                 vfs_node_t **entries, int *count)
{
    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    int ret = db_node_list(db, user_id, dir_id, entries, count);
    db_release(g_db_pool, db);
    return ret;
}

int vfs_create_file(uint64_t user_id, uint64_t parent_id, const char *name,
                    uint64_t size, const char *hash, const char *mime)
{
    if (!name) return -1;

    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    const char *m = mime ? mime : "application/octet-stream";
    uint64_t id = db_node_create_file(db, user_id, parent_id, name, size, hash, m);
    db_release(g_db_pool, db);

    if (id == 0) return -1;
    log_debug("vfs_create_file: user=%lu name=%s size=%lu -> id=%lu",
              (unsigned long)user_id, name, (unsigned long)size, (unsigned long)id);
    return 0;
}

int vfs_delete(uint64_t user_id, uint64_t node_id)
{
    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    int ret = db_node_delete(db, user_id, node_id);
    db_release(g_db_pool, db);
    return ret;
}

int vfs_move(uint64_t user_id, uint64_t node_id,
             uint64_t new_parent, const char *new_name)
{
    if (!new_name) return -1;

    MYSQL *db = db_acquire(g_db_pool);
    if (!db) return -1;

    int ret = db_node_move(db, user_id, node_id, new_parent, new_name);
    db_release(g_db_pool, db);
    return ret;
}