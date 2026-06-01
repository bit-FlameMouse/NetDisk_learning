/**
 * auth.h — 用户认证
 *
 * Register: SHA256(password + salt) → INSERT users
 * Login:   密码验证 → JWT 签发
 * Logout:  无状态，客户端发 BYE 即可
 */
#ifndef ROUTE_AUTH_H
#define ROUTE_AUTH_H

#include <stdint.h>

/**
 * 注册新用户
 * @param username  用户名
 * @param password  明文密码
 * @param user_id   输出：新用户 ID
 * @return 0=成功, -1=用户名已存在, -2=数据库错误
 */
int auth_register(const char *username, const char *password, uint64_t *user_id);

/**
 * 登录
 * @param username  用户名
 * @param password  明文密码
 * @param token_out 输出：JWT token（调用方 free）
 * @param user_id   输出：用户 ID
 * @return 0=成功, -1=用户名或密码错误
 */
int auth_login(const char *username, const char *password,
               char **token_out, uint64_t *user_id);

#endif
