/**
 * main.c — 服务端入口
 *
 * 职责：
 *   - 参数解析（-c config, -d daemon, -h help）
 *   - 加载配置文件
 *   - 初始化各子系统（日志、DB连接池、线程池、时间轮、epoll）
 *   - 信号处理（SIGINT/SIGTERM 优雅退出）
 *   - 进入事件循环 → 阻塞直到退出
 *
 * 全局变量定义在此文件，其他模块通过 include "global.h" 访问。
 */

#include "global.h"
#include "net/server.h"
#include "net/timer.h"
#include "base/log/log.h"
#include "base/thread_pool/thread_pool.h"
#include "data/db.h"
#include "../common/config/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ========================================================================
 * 全局变量定义（extern 声明在 global.h）
 * ======================================================================== */

config_t      *g_config      = NULL;   /* 全局配置 */
db_pool_t     *g_db_pool     = NULL;   /* MySQL 连接池 */
thread_pool_t *g_thread_pool = NULL;   /* 工作线程池 */
int            g_epoll_fd    = -1;     /* epoll 实例 fd */
int            g_listen_fd   = -1;     /* 监听 socket */
char           g_data_dir[256] = {0};  /* 文件存储根目录 */
char           g_tmp_dir[256]  = {0};  /* 临时文件目录 */
int            g_chunk_size    = 4194304; /* 上传分片大小 */

/* 退出标志（信号处理设置） */
static volatile int g_running = 1;

/* ========================================================================
 * 信号处理
 * ======================================================================== */

static void signal_handler(int sig)
{
    log_info("Received signal %d, shutting down...", sig);
    g_running = 0;
}

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);  /* 忽略 SIGPIPE，避免 write() 到已关闭连接时崩溃 */
}

/* ========================================================================
 * 守护进程化
 * ======================================================================== */

static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        /* 父进程退出 */
        _exit(0);
    }

    /* 子进程：创建新会话 */
    setsid();

    /* 第二次 fork 防止重新获取终端 */
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid > 0) {
        _exit(0);
    }

    /* 重定向标准输入输出到 /dev/null */
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

/* ========================================================================
 * 打印使用说明
 * ======================================================================== */

static void print_usage(const char *prog)
{
    printf("Usage: %s [-c config] [-d] [-h]\n", prog);
    printf("  -c config  配置文件路径（默认: netdisk.conf）\n");
    printf("  -d         守护进程模式\n");
    printf("  -h         显示帮助\n");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(int argc, char **argv)
{
    const char *config_path = "netdisk.conf";
    int daemon_mode = 0;

    /* ---- 1. 解析命令行参数 ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* ---- 2. 加载配置 ---- */
    g_config = config_load(config_path);
    if (!g_config) {
        fprintf(stderr, "Failed to load config: %s\n", config_path);
        return 1;
    }

    /* ---- 3. 守护进程化 ---- */
    if (daemon_mode) {
        daemonize();
    }

    /* ---- 4. 初始化日志 ---- */
    log_init(g_config->log_file, g_config->log_level,
             g_config->log_max_size, g_config->log_backup_count);
    log_info("NetDisk server starting...");
    log_info("Config loaded: %s", config_path);

    /* ---- 5. 设置信号处理 ---- */
    setup_signals();

    /* ---- 6. 初始化存储目录 ---- */
    strncpy(g_data_dir, g_config->data_dir, sizeof(g_data_dir) - 1);
    strncpy(g_tmp_dir,  g_config->tmp_dir,  sizeof(g_tmp_dir) - 1);
    g_chunk_size = g_config->chunk_size > 0 ? g_config->chunk_size : 4194304;

    mkdir(g_data_dir, 0755);
    mkdir(g_tmp_dir,  0755);

    /* 预创建 data/XX 子目录（0x00 - 0xFF 共 256 个） */
    for (int i = 0; i < 256; i++) {
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%02x", g_data_dir, i);
        mkdir(sub, 0755);
    }

    log_info("Storage: data=%s tmp=%s chunk=%d",
             g_data_dir, g_tmp_dir, g_chunk_size);

    /* ---- 7. 创建 DB 连接池 ---- */
    g_db_pool = db_pool_create(
        g_config->db_host, g_config->db_port,
        g_config->db_user, g_config->db_pass,
        g_config->db_name,
        g_config->db_pool_size, g_config->db_pool_timeout);
    if (!g_db_pool) {
        log_fatal("Failed to create DB pool");
    }

    /* ---- 8. 创建工作线程池 ---- */
    int workers = g_config->worker_count > 0
                  ? g_config->worker_count
                  : (int)sysconf(_SC_NPROCESSORS_ONLN) * 2;
    g_thread_pool = thread_pool_create(workers);
    if (!g_thread_pool) {
        log_fatal("Failed to create thread pool");
    }

    /* ---- 9. 初始化时间轮 ---- */
    if (timer_init(g_config->timer_slot_count) < 0) {
        log_fatal("Failed to init timer");
    }

    /* ---- 10. 启动服务端 ---- */
    g_listen_fd = server_init(g_config->server_host, g_config->server_port);
    if (g_listen_fd < 0) {
        log_fatal("Failed to init server");
    }

    log_info("NetDisk server started successfully");

    /* ---- 11. 进入事件循环（阻塞） ---- */
    server_event_loop(g_listen_fd);

    /* ---- 12. 优雅关闭 ---- */
    log_info("Shutting down...");

    server_shutdown();
    timer_destroy();

    /* 等待工作线程完成 */
    thread_pool_drain(g_thread_pool);
    thread_pool_destroy(g_thread_pool);
    g_thread_pool = NULL;

    /* 关闭 DB 连接池 */
    db_pool_destroy(g_db_pool);
    g_db_pool = NULL;

    /* 释放配置 */
    config_free(g_config);
    g_config = NULL;

    /* 关闭日志 */
    log_info("NetDisk server stopped");
    log_shutdown();

    return 0;
}
