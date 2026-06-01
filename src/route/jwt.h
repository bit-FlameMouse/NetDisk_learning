/**
 * jwt.h — JWT HS256 认证
 *
 * 无状态认证：内存验签，零数据库查询。
 * Token 结构：Header.Payload.Signature (Base64URL)
 */
#ifndef ROUTE_JWT_H
#define ROUTE_JWT_H

#include <stdint.h>

/** 签发 JWT（默认 900 秒有效期） */
char *jwt_generate(int user_id, const char *secret);

/**
 * 验证 JWT
 * @param token       JWT 字符串
 * @param secret      签名密钥
 * @param user_id_out 输出：用户 ID
 * @return  0=验证通过, -1=签名无效/格式错误, -2=已过期
 */
int jwt_verify(const char *token, const char *secret, int *user_id_out);

#endif
