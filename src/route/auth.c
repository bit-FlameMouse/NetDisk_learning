/**
 * auth.c — 用户认证实现
 *
 * 注册：SHA256(password + salt), salt 32 字节随机（/dev/urandom）
 * 登录：查 salt + hash → 验密码 → 签发 JWT
 */
#include "auth.h"
#include "jwt.h"
#include "../data/db.h"
#include "../global.h"
#include "../base/log/log.h"
#include "../../common/utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 生成随机盐（hex 字符串） */
static void gen_salt(char hex_out[65])
{
    uint8_t raw[32];
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp) {
        fread(raw, 1, 32, fp);
        fclose(fp);
    } else {
        /* fallback: 时间戳 + pid */
        srand((unsigned)time(NULL) ^ (unsigned)getpid());
        for (int i = 0; i < 32; i++) raw[i] = (uint8_t)(rand() & 0xFF);
    }
    for (int i = 0; i < 32; i++)
        snprintf(hex_out + i * 2, 3, "%02x", raw[i]);
    hex_out[64] = '\0';
}

/* 计算 SHA256(data) 的 hex 字符串 */
static void sha256_hex(const char *data, char hex_out[65])
{
    sha256_ctx_t *ctx = sha256_init();
    sha256_update(ctx, (const uint8_t *)data, strlen(data));
    sha256_final(ctx, hex_out);
    sha256_free(ctx);
}

int auth_register(const char *username, const char *password, uint64_t *user_id)
{
    if (!username || !password || !user_id) return -2;

    MYSQL *db = db_acquire(g_db_pool);
    if (!db) { log_error("auth_register: db_acquire failed"); return -2; }

    /* 检查用户名唯一性 */
    if (db_user_exists(db, username)) {
        db_release(g_db_pool, db);
        log_debug("auth_register: user '%s' already exists", username);
        return -1;
    }

    /* 生成盐 + 哈希 */
    char salt[65];
    gen_salt(salt);

    /* password_hash = SHA256(password + salt) */
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", password, salt);
    char pwd_hash[65];
    sha256_hex(combined, pwd_hash);

    *user_id = db_user_create(db, username, pwd_hash, salt);
    db_release(g_db_pool, db);

    if (*user_id == 0) {
        log_error("auth_register: db insert failed for '%s'", username);
        return -2;
    }

    log_info("User registered: %s (id=%lu)", username, (unsigned long)*user_id);
    return 0;
}

int auth_login(const char *username, const char *password,
               char **token_out, uint64_t *user_id)
{
    if (!username || !password || !token_out || !user_id) return -1;

    MYSQL *db = db_acquire(g_db_pool);
    if (!db) { log_error("auth_login: db_acquire failed"); return -1; }

    user_t user;
    if (!db_user_find(db, username, &user)) {
        db_release(g_db_pool, db);
        log_debug("auth_login: user '%s' not found", username);
        return -1;
    }

    /* 验证密码 */
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", password, user.salt);
    char expected[65];
    sha256_hex(combined, expected);

    if (strcmp(expected, user.password_hash) != 0) {
        db_release(g_db_pool, db);
        log_debug("auth_login: password mismatch for '%s'", username);
        return -1;
    }

    /* 更新最后登录时间 */
    db_user_touch_login(db, user.id);
    db_release(g_db_pool, db);

    /* 签发 JWT */
    *token_out = jwt_generate((int)user.id, g_config->jwt_secret);
    *user_id   = user.id;

    log_info("User logged in: %s (id=%lu)", username, (unsigned long)user.id);
    return 0;
}
