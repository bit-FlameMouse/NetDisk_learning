/**
 * config.h — 配置文件解析
 *
 * 负责人：老王
 *
 * 职责：
 *   - 解析 INI 格式的配置文件
 *   - 提供类型安全的字段读取接口
 *   - 提供默认值回退
 *
 * 依赖：无外部依赖
 */

#ifndef BASE_CONFIG_H
#define BASE_CONFIG_H

#include <stdint.h>

/* ========================================================================
 * 所有配置字段（与 config/netdisk.conf 一一对应）
 * ======================================================================== */

typedef struct {
    /* ---- [server] ---- */
    char    server_host[64];  // 服务器IP
    int     server_port;  // 服务器端口

    /* ---- [database] ---- */
    char    db_host[64];
    int     db_port;
    char    db_name[64];
    char    db_user[64];
    char    db_pass[128];
    int     db_pool_size;           // 连接池大小
    int     db_pool_timeout;        // 获取连接超时（秒）

    /* ---- [security] ---- */
    char    jwt_secret[128];        // JWT 签名密钥
    int     jwt_expire_sec;         // JWT 有效时长（秒）
    int     rate_limit_per_min;     // 每用户每分钟最大请求数
    int     password_salt_len;      // 密码 salt 字节数

    /* ---- [storage] ---- */
    char    data_dir[256];          // 文件存储根目录
    char    tmp_dir[256];           // 临时文件目录
    int     chunk_size;             // 上传分片大小（字节）
    int     upload_session_ttl;     // 上传会话有效期（小时）
    uint64_t quota_bytes;           // 单用户配额（字节；0=不限）

    /* ---- [server] 补充 ---- */
    int     max_connections;        // 最大并发连接数
    int     idle_timeout;           // 空闲超时（秒）

    /* ---- [cache] ---- */
    int     path_cache_size;        // LRU 缓存最大条目数
    int     path_cache_ttl;         // 缓存过期时间（秒）

    /* ---- [thread_pool] ---- */
    int     worker_count;           // 工作线程数
    int     queue_size;             // 任务队列长度

    /* ---- [log] ---- */
    int     log_level;              // 0=DEBUG 1=INFO 2=WARN 3=ERROR 4=FATAL
    char    log_file[256];          // 日志文件路径
    int     log_max_size;           // 单个日志文件最大字节数
    int     log_backup_count;       // 保留的历史日志文件数

    /* ---- [timer] ---- */
    int     timer_slot_count;       // 时间轮槽位数
    int     timer_tick_interval;    // tick 间隔（秒）
} config_t;

/* ========================================================================
 * 对外接口
 * ======================================================================== */

/**
 * 加载配置文件。
 *
 * 先加载默认值，再逐行解析 INI 文件覆盖。
 * 解析失败时保留默认值并打印 WARN。
 *
 * @param path  配置文件路径（如 "config/netdisk.conf"）
 * @return      堆上分配的 config_t，调用方负责 config_free()
 *              文件不存在返回 NULL
 */
config_t *config_load(const char *path);

/**
 * 释放 config_t 占用的内存。
 */
void config_free(config_t *cfg);

#endif /* BASE_CONFIG_H */
