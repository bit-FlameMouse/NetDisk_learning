/**
 * config.c — 客户端 INI 格式配置解析器
 *
 * 支持 [server] 和 [client] 两个 section。
 * 解析引擎为 common/ini_parser.c。
 */

#include "config.h"
#include "../../common/ini_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * 默认值
 * ======================================================================== */

static void set_defaults(client_config_t *cfg)
{
    strcpy(cfg->server_host,    "127.0.0.1");
    cfg->server_port            = 8443;
    strcpy(cfg->download_dir,   "./downloads");
    cfg->connect_timeout        = 10;
    cfg->recv_timeout           = 30;
}

/* ========================================================================
 * Section 枚举 → 不透明句柄映射
 * ======================================================================== */

enum {
    SEC_SERVER = 1,
    SEC_CLIENT = 2,
};

/* ========================================================================
 * ini_parse 回调 — 正在处理的配置指针
 * ======================================================================== */

static client_config_t *g_cfg;

static void *detect_section(const char *line)
{
    if (strcmp(line, "[server]") == 0) return (void *)(intptr_t)SEC_SERVER;
    if (strcmp(line, "[client]") == 0) return (void *)(intptr_t)SEC_CLIENT;
    return NULL;
}

static void apply_kv(void *section, const char *key, const char *value)
{
    int sec = (int)(intptr_t)section;

    switch (sec) {
    case SEC_SERVER:
        if (strcmp(key, "host") == 0) strcpy(g_cfg->server_host, value);
        if (strcmp(key, "port") == 0) g_cfg->server_port = atoi(value);
        break;

    case SEC_CLIENT:
        if (strcmp(key, "download_dir")   == 0) strcpy(g_cfg->download_dir, value);
        if (strcmp(key, "connect_timeout") == 0) g_cfg->connect_timeout = atoi(value);
        if (strcmp(key, "recv_timeout")    == 0) g_cfg->recv_timeout    = atoi(value);
        break;

    default:
        break;
    }
}

/* ========================================================================
 * 公开接口
 * ======================================================================== */

client_config_t *client_config_load(const char *path)
{
    client_config_t *cfg = calloc(1, sizeof(client_config_t));
    if (!cfg) {
        fprintf(stderr, "client_config_load: out of memory\n");
        return NULL;
    }
    set_defaults(cfg);

    /* 通过 ini_parse 引擎解析文件，用回调覆盖默认值 */
    g_cfg = cfg;
    ini_parse(path, detect_section, apply_kv);
    g_cfg = NULL;

    return cfg;
}

void client_config_free(client_config_t *cfg)
{
    free(cfg);
}
