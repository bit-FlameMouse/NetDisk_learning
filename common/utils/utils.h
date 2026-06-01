/**
 * utils.h — 通用工具函数
 *
 * 负责人：小李
 *
 * 职责：
 *   - SHA256 哈希计算（自实现，无 OpenSSL 依赖）
 *   - Base64 / Base64URL 编解码
 *   - 字符串处理、MIME 类型检测
 */

#ifndef BASE_UTILS_H
#define BASE_UTILS_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * SHA256
 * ======================================================================== */

#define SHA256_DIGEST_SIZE 32

/** 计算文件的 SHA256 哈希 */
int sha256_file(const char *path, char hex_out[65]);

/** 流式 SHA256 上下文（支持分块计算） */
typedef struct sha256_ctx sha256_ctx_t;

sha256_ctx_t *sha256_init(void);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, char hex_out[65]);
void sha256_free(sha256_ctx_t *ctx);

/* ========================================================================
 * Base64 / Base64URL
 * ======================================================================== */

/** Base64URL 编码（JWT 用） */
char *base64url_encode(const uint8_t *data, size_t len);

/** Base64URL 解码 */
int base64url_decode(const char *in, uint8_t *out, size_t out_size);

/* ========================================================================
 * 字符串工具
 * ======================================================================== */

/** 路径规范化：去冗余 /./ 和 //；返回静态缓冲区指针 */
const char *path_normalize(const char *path);

/** 取路径的父目录 */
void path_parent(const char *path, char *out, size_t out_size);

/** 取路径的文件名部分 */
const char *path_basename(const char *path);

/* ========================================================================
 * MIME 类型
 * ======================================================================== */

/** 根据文件扩展名返回 MIME 类型（静态字符串） */
const char *mime_by_ext(const char *filename);

/** 默认 MIME */
#define MIME_DEFAULT "application/octet-stream"

#endif /* BASE_UTILS_H */
