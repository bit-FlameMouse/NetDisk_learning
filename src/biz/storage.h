/**
 * storage.h — 存储引擎
 * 负责人：小赵
 */
#ifndef BIZ_STORAGE_H
#define BIZ_STORAGE_H

#include"../../common/proto/types.h"


/** 秒传检测：hash 已存在返回 1 + existing_id, 否则返回 0 */
int storage_check_hash(const char *hash, uint64_t *existing_id);


/** 初始化上传 */
int storage_upload_init(uint64_t user_id, uint64_t parent_id, const char *name,
                        uint64_t size, const char *hash, const char *mime,
                        uint64_t *upload_id);

/** 写入分片 */
int storage_write_chunk(uint64_t upload_id, uint32_t seq,
                        const uint8_t *data, uint32_t len);

/** 完成上传 */
int storage_upload_finish(uint64_t upload_id, vfs_node_t *result);

/** 开始下载：返回 fd（调用方 close） */
int storage_download_start(uint64_t file_id, int *out_fd, uint64_t *size, char *hash);

/** 带偏移量的读取（续传用） */
int storage_seek_read(int fd, uint64_t offset, uint8_t *buf, uint32_t len, uint32_t *read);

#endif
