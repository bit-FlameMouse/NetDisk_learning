/**
 * db.h — 数据库访问层
 *
 * 负责人：老周
 *
 * 上层模块不直接写 SQL、不接触 MYSQL/MYSQL_RES。
 * 所有数据库操作通过本层封装。
 */

#ifndef DATA_DB_H
#define DATA_DB_H

#include <mysql/mysql.h>
#include "../../common/proto/types.h"

/* ========================================================================
 * 连接池
 * ======================================================================== */

typedef struct db_pool db_pool_t;

db_pool_t *db_pool_create(const char *host, int port,
                           const char *user, const char *pass,
                           const char *name, int size, int timeout);
MYSQL      *db_acquire(db_pool_t *pool);
void        db_release(db_pool_t *pool, MYSQL *conn);
void        db_pool_destroy(db_pool_t *pool);

/* ========================================================================
 * 用户
 * ======================================================================== */

/** 创建用户，返回 user_id（失败 -1） */
uint64_t db_user_create(MYSQL *db, const char *username,
                         const char *pwd_hash, const char *salt);

/** 按用户名查找，填充 user_t */
int      db_user_find(MYSQL *db, const char *username, user_t *out);

/** 按 ID 查找 */
int      db_user_find_by_id(MYSQL *db, uint64_t id, user_t *out);

/** 用户名是否已存在 */
int      db_user_exists(MYSQL *db, const char *username);

/** 更新最后登录时间 */
void     db_user_touch_login(MYSQL *db, uint64_t user_id);

/* ========================================================================
 * 节点（虚拟文件系统）
 * ======================================================================== */

/** 在 parent_id 下查找名为 name 的节点，返回 node_id（失败 0） */
uint64_t db_node_find(MYSQL *db, uint64_t user_id,
                      uint64_t parent_id, const char *name);

/** 获取用户根节点 */
uint64_t db_node_root(MYSQL *db, uint64_t user_id);

/** 创建文件节点，返回 node_id */
uint64_t db_node_create_file(MYSQL *db, uint64_t user_id,
                              uint64_t parent_id, const char *name,
                              uint64_t size, const char *hash,
                              const char *mime);

/** 创建目录节点 */
uint64_t db_node_create_dir(MYSQL *db, uint64_t user_id,
                             uint64_t parent_id, const char *name);

/** 列出子节点，返回堆上分配的数组（调用方 free） */
int      db_node_list(MYSQL *db, uint64_t user_id, uint64_t parent_id,
                      vfs_node_t **entries, int *count);

/** 软删除 */
int      db_node_delete(MYSQL *db, uint64_t user_id, uint64_t node_id);

/** 移动/重命名 */
int      db_node_move(MYSQL *db, uint64_t user_id, uint64_t node_id,
                      uint64_t new_parent, const char *new_name);

/** 按 SHA256 哈希查找已有文件（秒传检测） */
uint64_t db_node_find_by_hash(MYSQL *db, const char *hash);

/* ========================================================================
 * 上传会话
 * ======================================================================== */

/** 创建上传会话，返回 upload_id */
uint64_t db_upload_create(MYSQL *db, uint64_t user_id,
                           uint64_t parent_id, const char *filename,
                           uint64_t total_size, int total_chunks);

/** 获取会话信息 */
int      db_upload_get(MYSQL *db, uint64_t upload_id,
                       upload_session_t *out);

/** 标记完成 */
void     db_upload_done(MYSQL *db, uint64_t upload_id);

#endif
