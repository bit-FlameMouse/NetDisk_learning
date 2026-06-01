/**
 * config.c — INI 格式配置文件解析器（服务端）
 *
 * 职责：
 *   - 定义 config_t 及其默认值
 *   - 通过 ini_parse 引擎解析 netdisk.conf
 *   - 8 个 section: server / database / security / storage / cache / thread / log / timer
 */

#include "config.h"
#include "../ini_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    cfg->chunk_size              = 4194304;
    cfg->upload_session_ttl      = 24;
    cfg->quota_bytes             = 10737418240ULL;

    /* ---- [cache] ---- */
    cfg->path_cache_size         = 10000;
    cfg->path_cache_ttl          = 60;

    /* ---- [thread_pool] ---- */
    cfg->worker_count            = 8;
    cfg->queue_size              = 4096;

    /* ---- [log] ---- */
    cfg->log_level               = 1;
    strcpy(cfg->log_file,        "/var/log/netdisk/server.log");
    cfg->log_max_size            = 104857600;
    cfg->log_backup_count        = 7;

    /* ---- [timer] ---- */
    cfg->timer_slot_count        = 60;
    cfg->timer_tick_interval     = 1;
}

/* ========================================================================
 * Section 枚举 → 不透明句柄映射
 * ======================================================================== */

enum {
    SEC_SERVER      = 1,
    SEC_DATABASE    = 2,
    SEC_SECURITY    = 3,
    SEC_STORAGE     = 4,
    SEC_CACHE       = 5,
    SEC_THREAD_POOL = 6,
    SEC_LOG         = 7,
    SEC_TIMER       = 8,
};

/* ========================================================================
 * ini_parse 回调 — 正在处理的配置指针
 * ======================================================================== */

static config_t *g_cfg;

static void *detect_section(const char *line)
{
    if (strcmp(line, "[server]")      == 0) return (void *)(intptr_t)SEC_SERVER;
    if (strcmp(line, "[database]")    == 0) return (void *)(intptr_t)SEC_DATABASE;
    if (strcmp(line, "[security]")    == 0) return (void *)(intptr_t)SEC_SECURITY;
    if (strcmp(line, "[storage]")     == 0) return (void *)(intptr_t)SEC_STORAGE;
    if (strcmp(line, "[cache]")       == 0) return (void *)(intptr_t)SEC_CACHE;
    if (strcmp(line, "[thread_pool]") == 0) return (void *)(intptr_t)SEC_THREAD_POOL;
    if (strcmp(line, "[log]")         == 0) return (void *)(intptr_t)SEC_LOG;
    if (strcmp(line, "[timer]")       == 0) return (void *)(intptr_t)SEC_TIMER;
    return NULL;
}

static void apply_kv(void *section, const char *key, const char *value)
{
    int sec = (int)(intptr_t)section;

    switch (sec) {
    case SEC_SERVER:
        if (strcmp(key, "host")            == 0) strcpy(g_cfg->server_host, value);
        if (strcmp(key, "port")            == 0) g_cfg->server_port = atoi(value);
        if (strcmp(key, "max_connections") == 0) g_cfg->max_connections = atoi(value);
        if (strcmp(key, "idle_timeout")    == 0) g_cfg->idle_timeout = atoi(value);
        break;

    case SEC_DATABASE:
        if (strcmp(key, "host")            == 0) strcpy(g_cfg->db_host, value);
        if (strcmp(key, "port")            == 0) g_cfg->db_port = atoi(value);
        if (strcmp(key, "name")            == 0) strcpy(g_cfg->db_name, value);
        if (strcmp(key, "user")            == 0) strcpy(g_cfg->db_user, value);
        if (strcmp(key, "password")        == 0) strcpy(g_cfg->db_pass, value);
        if (strcmp(key, "pool_size")       == 0) g_cfg->db_pool_size = atoi(value);
        if (strcmp(key, "pool_timeout")    == 0) g_cfg->db_pool_timeout = atoi(value);
        break;

    case SEC_SECURITY:
        if (strcmp(key, "jwt_secret")         == 0) strcpy(g_cfg->jwt_secret, value);
        if (strcmp(key, "jwt_expire_sec")     == 0) g_cfg->jwt_expire_sec = atoi(value);
        if (strcmp(key, "rate_limit_per_min") == 0) g_cfg->rate_limit_per_min = atoi(value);
        if (strcmp(key, "password_salt_len")  == 0) g_cfg->password_salt_len = atoi(value);
        break;

    case SEC_STORAGE:
        if (strcmp(key, "data_dir")           == 0) strcpy(g_cfg->data_dir, value);
        if (strcmp(key, "tmp_dir")            == 0) strcpy(g_cfg->tmp_dir, value);
        if (strcmp(key, "chunk_size")         == 0) g_cfg->chunk_size = atoi(value);
        if (strcmp(key, "upload_session_ttl") == 0) g_cfg->upload_session_ttl = atoi(value);
        if (strcmp(key, "quota_bytes")        == 0) g_cfg->quota_bytes = strtoull(value, NULL, 10);
        break;

    case SEC_CACHE:
        if (strcmp(key, "path_cache_size") == 0) g_cfg->path_cache_size = atoi(value);
        if (strcmp(key, "path_cache_ttl")  == 0) g_cfg->path_cache_ttl  = atoi(value);
        break;

    case SEC_THREAD_POOL:
        if (strcmp(key, "worker_count") == 0) g_cfg->worker_count = atoi(value);
        if (strcmp(key, "queue_size")   == 0) g_cfg->queue_size   = atoi(value);
        break;

    case SEC_LOG:
        if (strcmp(key, "level")         == 0) g_cfg->log_level  = atoi(value);
        if (strcmp(key, "file")          == 0) strcpy(g_cfg->log_file, value);
        if (strcmp(key, "max_file_size") == 0) g_cfg->log_max_size = atoi(value);
        if (strcmp(key, "backup_count")  == 0) g_cfg->log_backup_count = atoi(value);
        break;

    case SEC_TIMER:
        if (strcmp(key, "slot_count")    == 0) g_cfg->timer_slot_count    = atoi(value);
        if (strcmp(key, "tick_interval") == 0) g_cfg->timer_tick_interval = atoi(value);
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
    config_t *cfg = calloc(1, sizeof(config_t));
    if (!cfg) {
        fprintf(stderr, "config_load: out of memory\n");
        return NULL;
    }
    config_set_defaults(cfg);

    /* 通过 ini_parse 引擎解析文件，用回调覆盖默认值 */
    g_cfg = cfg;
    ini_parse(path, detect_section, apply_kv);
    g_cfg = NULL;

    return cfg;
}

void config_free(config_t *cfg)
{
    free(cfg);
}
