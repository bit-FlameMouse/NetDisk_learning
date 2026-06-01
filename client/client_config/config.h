/**
 * config.h — 客户端配置文件解析
 *
 * 职责：
 *   - 解析 client.conf INI 格式配置文件
 *   - 提供合理默认值
 *   - 命令行参数可覆盖配置值
 */

#ifndef CLIENT_CONFIG_H
#define CLIENT_CONFIG_H

/* ========================================================================
 * 客户端配置字段
 * ======================================================================== */

typedef struct {
    /* ---- [server] ---- */
    char server_host[64];       /* 服务端 IP/域名 */
    int  server_port;           /* 服务端端口 */

    /* ---- [client] ---- */
    char download_dir[256];     /* 默认下载目录 */
    int  connect_timeout;       /* 连接超时（秒） */
    int  recv_timeout;          /* 接收超时（秒） */
} client_config_t;

/* ========================================================================
 * 对外接口
 * ======================================================================== */

/**
 * 加载客户端配置文件。
 *
 * 先填默认值，再逐行解析 INI 文件覆盖。
 * 文件不存在时返回默认值配置（不报错）。
 *
 * @param path  配置文件路径（如 "client/client.conf"）
 * @return      堆上分配的 client_config_t
 */
client_config_t *client_config_load(const char *path);

/**
 * 释放配置。
 */
void client_config_free(client_config_t *cfg);

#endif /* CLIENT_CONFIG_H */
