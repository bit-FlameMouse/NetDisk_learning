/**
 * config.c — INI 格式配置文件解析器
 *
 * 负责人：老王
 *
 * 职责：
 *   - 加载并解析 config/netdisk.conf
 *   - 提供合理的默认值（文件不存在时仍可用）
 *   - 忽略空行、注释行、无法识别的 key
 *   - 解析失败时打印 WARN 保留默认值，不中止
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* 配置行最大长度 */
#define LINE_MAX    512

/* ========================================================================
 * 默认值
 * ======================================================================== */

static void config_set_defaults(config_t *cfg)
{
    /* ---- [server] ---- */
    strcpy(cfg->server_host,     "0.0.0.0");
    cfg->server_port             = 8443;
    cfg->max_connections         = 10240;
    cfg->idle_timeout            = 60;

    /* ---- [database] ---- */
    strcpy(cfg->db_host,         "127.0.0.1");
    cfg->db_port                 = 3306;
    strcpy(cfg->db_name,         "netdisk");
    strcpy(cfg->db_user,         "netdisk");
    strcpy(cfg->db_pass,         "");
    cfg->db_pool_size            = 16;
    cfg->db_pool_timeout         = 5;

    /* ---- [security] ---- */
    strcpy(cfg->jwt_secret,      "CHANGE_ME");
    cfg->jwt_expire_sec          = 900;
    cfg->rate_limit_per_min      = 30;
    cfg->password_salt_len       = 32;

    /* ---- [storage] ---- */
    strcpy(cfg->data_dir,        "/data/netdisk/files");
    strcpy(cfg->tmp_dir,         "/data/netdisk/tmp");
    cfg->chunk_size              = 4194304;   /* 4 MB */
    cfg->upload_session_ttl      = 24;
    cfg->quota_bytes             = 10737418240ULL; /* 10 GB */

    /* ---- [cache] ---- */
    cfg->path_cache_size         = 10000;
    cfg->path_cache_ttl          = 60;

    /* ---- [thread_pool] ---- */
    cfg->worker_count            = 8;
    cfg->queue_size              = 4096;

    /* ---- [log] ---- */
    cfg->log_level               = 1;          /* INFO */
    strcpy(cfg->log_file,        "/var/log/netdisk/server.log");
    cfg->log_max_size            = 104857600;  /* 100 MB */
    cfg->log_backup_count        = 7;

    /* ---- [timer] ---- */
    cfg->timer_slot_count        = 60;
    cfg->timer_tick_interval     = 1;
}

/* ========================================================================
 * 内部辅助
 * ======================================================================== */

/**
 * 去掉字符串首尾空白字符（原地修改）。
 */
static char *trim(char *s)
{
    /* 去尾部空白 */
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }

    /* 去首部空白 */
    while (isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/**
 * 解析 "key = value" 行，返回 key 和 value。
 * 返回 1 表示成功识别，0 表示无效行（注释、空行、无等号）。
 */
static int parse_kv(const char *line, char *key, size_t key_size,
                    char *value, size_t value_size)
{
    /* 找等号 */
    const char *eq = strchr(line, '=');
    if (eq == NULL) return 0;

    /* 提取 key */
    size_t klen = eq - line;
    if (klen >= key_size) klen = key_size - 1;
    memcpy(key, line, klen);
    key[klen] = '\0';
    strcpy(key, trim(key));

    if (strlen(key) == 0) return 0;

    /* 提取 value */
    const char *val_start = eq + 1;
    strncpy(value, val_start, value_size - 1);
    value[value_size - 1] = '\0';
    strcpy(value, trim(value));

    return 1;
}

/* ========================================================================
 * Section → 字段映射
 *
 * 每个解析函数处理一个 [section] 下的所有 key。
 * 不认识的值保留默认值，不报错。
 * ======================================================================== */

typedef enum {
    SEC_NONE,
    SEC_SERVER,
    SEC_DATABASE,
    SEC_SECURITY,
    SEC_STORAGE,
    SEC_CACHE,
    SEC_THREAD_POOL,
    SEC_LOG,
    SEC_TIMER,
} section_t;

static section_t detect_section(const char *line)
{
    if (strcmp(line, "[server]")      == 0) return SEC_SERVER;
    if (strcmp(line, "[database]")    == 0) return SEC_DATABASE;
    if (strcmp(line, "[security]")    == 0) return SEC_SECURITY;
    if (strcmp(line, "[storage]")     == 0) return SEC_STORAGE;
    if (strcmp(line, "[cache]")       == 0) return SEC_CACHE;
    if (strcmp(line, "[thread_pool]") == 0) return SEC_THREAD_POOL;
    if (strcmp(line, "[log]")         == 0) return SEC_LOG;
    if (strcmp(line, "[timer]")       == 0) return SEC_TIMER;
    return SEC_NONE;
}

static void apply_kv(config_t *cfg, section_t sec,
                     const char *key, const char *value)
{
    switch (sec) {
    case SEC_SERVER:
        if (strcmp(key, "host")            == 0) strcpy(cfg->server_host, value);
        if (strcmp(key, "port")            == 0) cfg->server_port = atoi(value);
        if (strcmp(key, "max_connections") == 0) cfg->max_connections = atoi(value);
        if (strcmp(key, "idle_timeout")    == 0) cfg->idle_timeout = atoi(value);
        break;

    case SEC_DATABASE:
        if (strcmp(key, "host")            == 0) strcpy(cfg->db_host, value);
        if (strcmp(key, "port")            == 0) cfg->db_port = atoi(value);
        if (strcmp(key, "name")            == 0) strcpy(cfg->db_name, value);
        if (strcmp(key, "user")            == 0) strcpy(cfg->db_user, value);
        if (strcmp(key, "password")        == 0) strcpy(cfg->db_pass, value);
        if (strcmp(key, "pool_size")       == 0) cfg->db_pool_size = atoi(value);
        if (strcmp(key, "pool_timeout")    == 0) cfg->db_pool_timeout = atoi(value);
        break;

    case SEC_SECURITY:
        if (strcmp(key, "jwt_secret")      == 0) strcpy(cfg->jwt_secret, value);
        if (strcmp(key, "jwt_expire_sec")  == 0) cfg->jwt_expire_sec = atoi(value);
        if (strcmp(key, "rate_limit_per_min") == 0) cfg->rate_limit_per_min = atoi(value);
        if (strcmp(key, "password_salt_len")  == 0) cfg->password_salt_len = atoi(value);
        break;

    case SEC_STORAGE:
        if (strcmp(key, "data_dir")            == 0) strcpy(cfg->data_dir, value);
        if (strcmp(key, "tmp_dir")             == 0) strcpy(cfg->tmp_dir, value);
        if (strcmp(key, "chunk_size")          == 0) cfg->chunk_size = atoi(value);
        if (strcmp(key, "upload_session_ttl")  == 0) cfg->upload_session_ttl = atoi(value);
        if (strcmp(key, "quota_bytes")         == 0) {
            cfg->quota_bytes = strtoull(value, NULL, 10);
        }
        break;

    case SEC_CACHE:
        if (strcmp(key, "path_cache_size") == 0) cfg->path_cache_size = atoi(value);
        if (strcmp(key, "path_cache_ttl")  == 0) cfg->path_cache_ttl  = atoi(value);
        break;

    case SEC_THREAD_POOL:
        if (strcmp(key, "worker_count") == 0) cfg->worker_count = atoi(value);
        if (strcmp(key, "queue_size")   == 0) cfg->queue_size   = atoi(value);
        break;

    case SEC_LOG:
        if (strcmp(key, "level")         == 0) cfg->log_level  = atoi(value);
        if (strcmp(key, "file")          == 0) strcpy(cfg->log_file, value);
        if (strcmp(key, "max_file_size") == 0) cfg->log_max_size = atoi(value);
        if (strcmp(key, "backup_count")  == 0) cfg->log_backup_count = atoi(value);
        break;

    case SEC_TIMER:
        if (strcmp(key, "slot_count")    == 0) cfg->timer_slot_count    = atoi(value);
        if (strcmp(key, "tick_interval") == 0) cfg->timer_tick_interval = atoi(value);
        break;

    default:
        break;
    }
}

/* ========================================================================
 * 公开接口
 * ======================================================================== */

config_t *config_load(const char *path)
{
    FILE *fp;
    char line[LINE_MAX];
    section_t current_section = SEC_NONE;
    int lineno = 0;

    /* ---- ① 分配内存 + 填默认值 ---- */
    config_t *cfg = calloc(1, sizeof(config_t));
    if (cfg == NULL) {
        fprintf(stderr, "config_load: out of memory\n");
        return NULL;
    }
    config_set_defaults(cfg);

    /* ---- ② 打开文件 ---- */
    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "config_load: cannot open %s, using defaults\n", path);
        return cfg;  /* 文件不存在 → 返回默认值，不报错 */
    }

    /* ---- ③ 逐行解析 ---- */
    while (fgets(line, sizeof(line), fp) != NULL) {
        lineno++;

        /* 去掉末尾换行符 */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        char *trimmed = trim(line);

        /* 跳过空行和注释 */
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;

        /* [section] 行 */
        if (trimmed[0] == '[') {
            section_t sec = detect_section(trimmed);
            if (sec != SEC_NONE) {
                current_section = sec;
            } else {
                fprintf(stderr, "config: line %d: unknown section '%s'\n",
                        lineno, trimmed);
            }
            continue;
        }

        /* key = value 行 */
        char key[128], value[256];
        if (parse_kv(trimmed, key, sizeof(key), value, sizeof(value))) {
            apply_kv(cfg, current_section, key, value);
        } else {
            fprintf(stderr, "config: line %d: invalid syntax '%s'\n",
                    lineno, trimmed);
        }
    }

    fclose(fp);
    return cfg;
}

void config_free(config_t *cfg)
{
    free(cfg);
}
