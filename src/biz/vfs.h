/**
 * vfs.h — 虚拟文件系统
 *
 * 基于 MySQL 邻接表模型实现虚拟目录树。
 * 路径与物理存储完全解耦。
 */
#ifndef BIZ_VFS_H
#define BIZ_VFS_H

#include "../../common/proto/types.h"
#include <stdint.h>

/**
 * 路径 → node_id（逐级 SQL 解析）
 * @param user_id   用户 ID
 * @param path      绝对路径（如 "/docs/work/report.pdf"）
 * @param node_id   输出：节点 ID
 * @return 0=成功, -1=路径不存在
 */
int vfs_resolve_path(uint64_t user_id, const char *path, uint64_t *node_id);

/** 创建目录 */
int vfs_mkdir(uint64_t user_id, uint64_t parent_id, const char *name);

/** 列出目录 */
int vfs_list_dir(uint64_t user_id, uint64_t dir_id,
                 vfs_node_t **entries, int *count);

/** 创建文件记录（上传完成后调用） */
int vfs_create_file(uint64_t user_id, uint64_t parent_id, const char *name,
                    uint64_t size, const char *hash, const char *mime);

/** 软删除 */
int vfs_delete(uint64_t user_id, uint64_t node_id);

/** 移动/重命名 */
int vfs_move(uint64_t user_id, uint64_t node_id,
             uint64_t new_parent, const char *new_name);

#endif