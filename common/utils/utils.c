/**
 * utils.c — 通用工具函数实现
 *
 * 负责人：小李
 *
 * SHA256：自实现（RFC 6234 简化版），无 OpenSSL 依赖。
 */

#include "utils.h"
#include "../proto/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ========================================================================
 * SHA256（自实现精简版）
 * ======================================================================== */

#define SHA256_BLOCK_SIZE 64

struct sha256_ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[SHA256_BLOCK_SIZE];
};

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define S0(x) (ROTR(x,7)^ROTR(x,18)^((x)>>3))
#define S1(x) (ROTR(x,17)^ROTR(x,19)^((x)>>10))
#define S2(x) (ROTR(x,2)^ROTR(x,13)^ROTR(x,22))
#define S3(x) (ROTR(x,6)^ROTR(x,11)^ROTR(x,25))
#define F0(x,y,z) (((x)&(y))|((z)&((x)|(y))))
#define F1(x,y,z) ((z)^((x)&((y)^(z))))

static void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t W[64];
    for (int i = 0; i < 16; i++)
        W[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)
              |((uint32_t)block[i*4+2]<<8)|(uint32_t)block[i*4+3];
    for (int i = 16; i < 64; i++)
        W[i] = S1(W[i-2]) + W[i-7] + S0(W[i-15]) + W[i-16];

    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],
             e=state[4],f=state[5],g=state[6],h=state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + S3(e) + F1(e,f,g) + K[i] + W[i];
        uint32_t t2 = S2(a) + F0(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
    state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}

sha256_ctx_t *sha256_init(void)
{
    sha256_ctx_t *ctx = calloc(1, sizeof(sha256_ctx_t));
    if (!ctx) return NULL;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
    return ctx;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->count % 64] = data[i];
        ctx->count++;
        if (ctx->count % 64 == 0) sha256_transform(ctx->state, ctx->buf);
    }
}

void sha256_final(sha256_ctx_t *ctx, char hex_out[65])
{
    uint8_t pad[128] = {0x80};
    uint64_t bits = ctx->count * 8;
    int padlen = (ctx->count % 64 < 56) ? (56 - ctx->count % 64)
                                        : (120 - ctx->count % 64);
    sha256_update(ctx, pad, padlen);
    /* 追加原始长度（大端 64-bit） */
    uint8_t len_buf[8];
    for (int i = 7; i >= 0; i--) { len_buf[i] = bits & 0xFF; bits >>= 8; }
    sha256_update(ctx, len_buf, 8);

    for (int i = 0; i < 8; i++)
        snprintf(hex_out + i*8, 9, "%08x", ctx->state[i]);
    hex_out[64] = '\0';
}

void sha256_free(sha256_ctx_t *ctx) { free(ctx); }

int sha256_file(const char *path, char hex_out[65])
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    sha256_ctx_t *ctx = sha256_init();
    uint8_t buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) sha256_update(ctx, buf, n);
    fclose(fp);
    sha256_final(ctx, hex_out);
    sha256_free(ctx);
    return 0;
}

/* ========================================================================
 * Base64URL
 * ======================================================================== */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

char *base64url_encode(const uint8_t *data, size_t len)
{
    size_t out_len = ((len + 2) / 3) * 4 + 1;
    char *out = malloc(out_len);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i+1 < len) v |= (uint32_t)data[i+1] << 8;
        if (i+2 < len) v |= data[i+2];
        out[j++] = b64_table[(v>>18)&0x3F];
        out[j++] = b64_table[(v>>12)&0x3F];
        out[j++] = (i+1 < len) ? b64_table[(v>>6)&0x3F] : '=';
        out[j++] = (i+2 < len) ? b64_table[v&0x3F]       : '=';
    }
    out[j] = '\0';
    return out;
}

static int b64url_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

int base64url_decode(const char *in, uint8_t *out, size_t out_size)
{
    if (!in || !out || out_size == 0) return -1;

    size_t in_len = strlen(in);
    size_t pad = 0;
    /* Base64URL 通常无 = 填充，但兼容处理 */
    while (in_len > 0 && in[in_len - 1] == '=') { in_len--; pad++; }

    size_t opos = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        int v[4];
        int valid = 0;
        for (int j = 0; j < 4; j++) {
            if (i + j < in_len) {
                v[j] = b64url_val(in[i + j]);
                if (v[j] >= 0) valid++;
                else v[j] = 0;
            } else {
                v[j] = 0;
            }
        }
        if (valid == 0) break;

        if (opos < out_size) out[opos++] = (uint8_t)((v[0] << 2) | (v[1] >> 4));
        if (opos < out_size && valid >= 3)
            out[opos++] = (uint8_t)((v[1] << 4) | (v[2] >> 2));
        if (opos < out_size && valid >= 4)
            out[opos++] = (uint8_t)((v[2] << 6) | v[3]);
    }
    return (int)opos;
}

/* ========================================================================
 * 路径工具
 * ======================================================================== */

const char *path_normalize(const char *path)
{
    static char buf[PATH_MAX_LEN];
    char *p = buf;
    for (const char *s = path; *s && (p - buf) < PATH_MAX_LEN-1; s++) {
        if (*s == '/' && *(s+1) == '/') continue;
        if (*s == '.' && *(s+1) == '/')  { s++; continue; }
        *p++ = *s;
    }
    *p = '\0';
    return (*buf == '/') ? buf : "";
}

void path_parent(const char *path, char *out, size_t out_size)
{
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) snprintf(out, out_size, "/");
    else snprintf(out, out_size, "%.*s", (int)(slash - path), path);
}

const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ========================================================================
 * MIME 类型
 * ======================================================================== */

typedef struct { const char *ext; const char *mime; } mime_entry_t;

static const mime_entry_t g_mimes[] = {
    {"pdf",   "application/pdf"},
    {"jpg",   "image/jpeg"},
    {"jpeg",  "image/jpeg"},
    {"png",   "image/png"},
    {"gif",   "image/gif"},
    {"txt",   "text/plain"},
    {"html",  "text/html"},
    {"css",   "text/css"},
    {"js",    "application/javascript"},
    {"json",  "application/json"},
    {"xml",   "application/xml"},
    {"zip",   "application/zip"},
    {"tar",   "application/x-tar"},
    {"gz",    "application/gzip"},
    {"mp3",   "audio/mpeg"},
    {"mp4",   "video/mp4"},
    {"iso",   "application/octet-stream"},
    {"docx",  "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"xlsx",  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {NULL, NULL}
};

const char *mime_by_ext(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot) return MIME_DEFAULT;
    const char *ext = dot + 1;
    for (int i = 0; g_mimes[i].ext; i++)
        if (strcasecmp(ext, g_mimes[i].ext) == 0) return g_mimes[i].mime;
    return MIME_DEFAULT;
}
