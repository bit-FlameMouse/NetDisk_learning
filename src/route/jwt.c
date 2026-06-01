/**
 * jwt.c — JWT HS256 实现
 *
 * HMAC-SHA256 签名 + Base64URL 编解码。
 * 使用 common/utils 中的 SHA256 自实现，无 OpenSSL 依赖。
 */
#include "jwt.h"
#include "../../common/utils/utils.h"
#include "../base/log/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define JWT_EXPIRE_SEC 900  /* 默认 15 分钟 */

/* ========================================================================
 * HMAC-SHA256
 * ======================================================================== */

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *msg, size_t msg_len,
                        uint8_t out[32])
{
    uint8_t key_block[64];
    memset(key_block, 0, 64);

    if (key_len > 64) {
        /* key 太长，先 hash */
        sha256_ctx_t *ctx = sha256_init();
        sha256_update(ctx, key, key_len);
        char hex[65];
        sha256_final(ctx, hex);
        sha256_free(ctx);
        /* 将 hex 转回 binary 32 字节 */
        for (int i = 0; i < 32; i++) {
            unsigned int byte;
            sscanf(hex + i * 2, "%2x", &byte);
            key_block[i] = (uint8_t)byte;
        }
    } else {
        memcpy(key_block, key, key_len);
    }

    /* ipad = key_block XOR 0x36 */
    uint8_t ipad[64];
    for (int i = 0; i < 64; i++) ipad[i] = key_block[i] ^ 0x36;

    /* opad = key_block XOR 0x5c */
    uint8_t opad[64];
    for (int i = 0; i < 64; i++) opad[i] = key_block[i] ^ 0x5c;

    /* inner = SHA256(ipad || message) */
    sha256_ctx_t *ctx = sha256_init();
    sha256_update(ctx, ipad, 64);
    sha256_update(ctx, msg, msg_len);
    char inner_hex[65];
    sha256_final(ctx, inner_hex);
    sha256_free(ctx);

    /* 将 inner_hex 转回 binary */
    uint8_t inner_bin[32];
    for (int i = 0; i < 32; i++) {
        unsigned int byte;
        sscanf(inner_hex + i * 2, "%2x", &byte);
        inner_bin[i] = (uint8_t)byte;
    }

    /* outer = SHA256(opad || inner) */
    ctx = sha256_init();
    sha256_update(ctx, opad, 64);
    sha256_update(ctx, inner_bin, 32);
    char outer_hex[65];
    sha256_final(ctx, outer_hex);
    sha256_free(ctx);

    /* 将 hex 结果转为 binary 输出 */
    for (int i = 0; i < 32; i++) {
        unsigned int byte;
        sscanf(outer_hex + i * 2, "%2x", &byte);
        out[i] = (uint8_t)byte;
    }
}

/* ========================================================================
 * JWT 生成与验证
 * ======================================================================== */

char *jwt_generate(int user_id, const char *secret)
{
    /* 构造 Header */
    const char *header_json = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    char *header_b64 = base64url_encode((const uint8_t *)header_json,
                                         strlen(header_json));

    /* 构造 Payload */
    time_t now = time(NULL);
    char payload_json[256];
    snprintf(payload_json, sizeof(payload_json),
             "{\"sub\":%d,\"iat\":%ld,\"exp\":%ld}",
             user_id, (long)now, (long)(now + JWT_EXPIRE_SEC));
    char *payload_b64 = base64url_encode((const uint8_t *)payload_json,
                                          strlen(payload_json));

    /* 构造签名输入 */
    size_t sign_input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    char *sign_input = malloc(sign_input_len + 1);
    snprintf(sign_input, sign_input_len + 1, "%s.%s", header_b64, payload_b64);

    /* HMAC-SHA256 签名 */
    uint8_t sig_bin[32];
    hmac_sha256((const uint8_t *)secret, strlen(secret),
                (const uint8_t *)sign_input, sign_input_len, sig_bin);
    char *sig_b64 = base64url_encode(sig_bin, 32);

    /* 组装完整 Token */
    size_t token_len = sign_input_len + 1 + strlen(sig_b64);
    char *token = malloc(token_len + 1);
    snprintf(token, token_len + 1, "%s.%s", sign_input, sig_b64);

    free(header_b64);
    free(payload_b64);
    free(sign_input);
    free(sig_b64);

    return token;
}

int jwt_verify(const char *token, const char *secret, int *user_id_out)
{
    if (!token || !secret || !user_id_out) return -1;

    /* 分割 Header.Payload.Signature */
    char tok_copy[512];
    strncpy(tok_copy, token, sizeof(tok_copy) - 1);
    tok_copy[sizeof(tok_copy) - 1] = '\0';

    char *save1;
    char *header_b64 = strtok_r(tok_copy, ".", &save1);
    char *payload_b64 = strtok_r(NULL, ".", &save1);
    char *sig_b64 = strtok_r(NULL, ".", &save1);

    if (!header_b64 || !payload_b64 || !sig_b64) {
        return -1;  /* 格式错误 */
    }

    /* 验证签名 */
    size_t sign_input_len = strlen(header_b64) + 1 + strlen(payload_b64);
    char *sign_input = malloc(sign_input_len + 1);
    snprintf(sign_input, sign_input_len + 1, "%s.%s", header_b64, payload_b64);

    uint8_t expected_sig[32];
    hmac_sha256((const uint8_t *)secret, strlen(secret),
                (const uint8_t *)sign_input, sign_input_len, expected_sig);

    /* 解码收到的签名 */
    uint8_t actual_sig[32];
    int sig_len = base64url_decode(sig_b64, actual_sig, sizeof(actual_sig));
    if (sig_len != 32 || memcmp(expected_sig, actual_sig, 32) != 0) {
        free(sign_input);
        return -1;  /* 签名无效 */
    }
    free(sign_input);

    /* 解码 Payload */
    uint8_t payload_json[256];
    int payload_len = base64url_decode(payload_b64, payload_json, sizeof(payload_json) - 1);
    if (payload_len < 0) return -1;
    payload_json[payload_len] = '\0';

    /* 解析 JSON：提取 sub, exp */
    int sub = 0;
    long exp = 0;
    char *sub_p = strstr((char *)payload_json, "\"sub\":");
    char *exp_p = strstr((char *)payload_json, "\"exp\":");
    if (sub_p) sub = atoi(sub_p + 6);
    if (exp_p) exp = atol(exp_p + 6);

    /* 检查过期 */
    if (exp > 0 && time(NULL) > exp) {
        return -2;  /* 已过期 */
    }

    *user_id_out = sub;
    return 0;
}
