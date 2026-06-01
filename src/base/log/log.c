/*
* log.c - 日志实现
*/

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

/* ========================================================================
 * 常量
 * ======================================================================== */

#define RING_SIZE        (256 * 1024)   /* 环形缓冲区 256KB */
#define TIMESTAMP_LEN    20             /* "2026-06-01 08:57:31" */
#define LOG_LINE_MAX     1024           /* 单条日志最大长度 */
#define FLUSH_INTERVAL   1              /* 刷盘间隔（秒） */

/* ========================================================================
 * 环形缓冲区条目
 * ======================================================================== */

typedef struct {
    int   len;                         /* 有符号长度：>0=有效，0=空闲 */
    char  data[LOG_LINE_MAX];
} ring_entry_t;

/* ========================================================================
 * 全局状态
 * ======================================================================== */

static struct {
    /* ---- 环形缓冲区 ---- */
    ring_entry_t   *ring;              /* 堆分配 */
    int             ring_size;         /* 条目数 */
    int             head;              /* 生产者写入位置 */
    int             tail;              /* 消费者读取位置 */
    pthread_mutex_t lock;
    pthread_cond_t  cond;              /* 缓冲区空 → Worker 等，缓冲区有数据 → 后台线程等 */

    /* ---- 文件 ---- */
    FILE           *fp;
    char            file_path[256];
    int             max_size;          /* 轮转阈值（字节） */
    int             backups;           /* 保留份数 */

    /* ---- 配置 ---- */
    int             level;             /* 最低记录级别 */
    volatile int    running;           /* 0 = 停止 */

    /* ---- 后台线程 ---- */
    pthread_t       flush_thread;
} g_log;

/* ========================================================================
 * 级别标签
 * ======================================================================== */

static const char *level_label(int level)
{
    switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO:  return "INFO ";
    case LOG_WARN:  return "WARN ";
    case LOG_ERROR: return "ERROR";
    case LOG_FATAL: return "FATAL";
    default:        return "?????";
    }
}

/* ========================================================================
 * 文件轮转
 * ======================================================================== */

static void log_rotate(void)
{
    if (g_log.fp == NULL || g_log.max_size <= 0) return;

    long sz = ftell(g_log.fp);
    if (sz < g_log.max_size) return;

    /* 关闭当前文件 */
    fclose(g_log.fp);

    /* 轮转旧文件：.3 → .4, .2 → .3, .1 → .2 */
    for (int i = g_log.backups - 1; i >= 0; i--) {
        char old_name[300], new_name[300];
        if (i == 0) {
            snprintf(old_name, sizeof(old_name), "%s",    g_log.file_path);
        } else {
            snprintf(old_name, sizeof(old_name), "%s.%d", g_log.file_path, i);
        }
        snprintf(new_name, sizeof(new_name), "%s.%d", g_log.file_path, i + 1);
        rename(old_name, new_name);
    }

    /* 打开新文件 */
    g_log.fp = fopen(g_log.file_path, "a");
    if (g_log.fp == NULL) {
        fprintf(stderr, "log_rotate: cannot reopen %s\n", g_log.file_path);
    }
}

/* ========================================================================
 * 后台刷盘线程
 * ======================================================================== */

static void *flush_worker(void *arg)
{
    (void)arg;

    while (g_log.running) {
        pthread_mutex_lock(&g_log.lock);

        /* 等待数据或停止信号 */
        while (g_log.running && g_log.ring[g_log.tail].len == 0) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += FLUSH_INTERVAL;
            pthread_cond_timedwait(&g_log.cond, &g_log.lock, &ts);
        }

        if (!g_log.running) {
            pthread_mutex_unlock(&g_log.lock);
            break;
        }

        /* 批量消费环形缓冲区 */
        while (g_log.ring[g_log.tail].len > 0) {
            ring_entry_t *entry = &g_log.ring[g_log.tail];

            /* 写入文件 */
            if (g_log.fp) {
                fwrite(entry->data, 1, entry->len, g_log.fp);
                fflush(g_log.fp);
                log_rotate();
            }
            /* 同时输出到 stderr（方便调试） */
            fprintf(stderr, "%.*s", entry->len, entry->data);

            /* 标记为空闲 */
            entry->len = 0;
            g_log.tail = (g_log.tail + 1) % g_log.ring_size;
        }

        /* 通知可能阻塞的生产者 */
        pthread_cond_broadcast(&g_log.cond);
        pthread_mutex_unlock(&g_log.lock);
    }

    return NULL;
}

/* ========================================================================
 * 公开接口
 * ======================================================================== */

void log_init(const char *file_path, int level,
              int max_size, int backups)
{
    memset(&g_log, 0, sizeof(g_log));

    g_log.level    = level;
    g_log.max_size = max_size;
    g_log.backups  = backups;
    g_log.running  = 1;

    /* 分配环形缓冲区 */
    g_log.ring_size = RING_SIZE / sizeof(ring_entry_t);
    g_log.ring = calloc(g_log.ring_size, sizeof(ring_entry_t));
    if (g_log.ring == NULL) {
        fprintf(stderr, "log_init: out of memory\n");
        return;
    }

    pthread_mutex_init(&g_log.lock, NULL);
    pthread_cond_init(&g_log.cond, NULL);

    /* 打开日志文件 */
    if (file_path && file_path[0]) {
        strncpy(g_log.file_path, file_path, sizeof(g_log.file_path) - 1);
        g_log.fp = fopen(file_path, "a");
        if (g_log.fp == NULL) {
            fprintf(stderr, "log_init: cannot open %s, logging to stderr only\n",
                    file_path);
        }
    }

    /* 启动后台刷盘线程 */
    pthread_create(&g_log.flush_thread, NULL, flush_worker, NULL);
}

void log_write(int level, const char *file, int line,
               const char *fmt, ...)
{
    if (level < g_log.level) return;

    /* ---- 格式化时间戳 ---- */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char ts[TIMESTAMP_LEN];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    /* ---- 提取文件名（仅保留 basename，去掉路径前缀） ---- */
    const char *basename = strrchr(file, '/');
    if (basename) {
        basename++;  /* 跳过 '/' */
    } else {
        basename = strrchr(file, '\\');
        if (basename) basename++;
        else          basename = file;
    }

    /* ---- 构造日志行 ---- */
    char line_buf[LOG_LINE_MAX];
    int  prefix_len = snprintf(line_buf, sizeof(line_buf),
                               "[%s] %s %s:%d ",
                               ts, level_label(level), basename, line);

    if (prefix_len < (int)sizeof(line_buf)) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line_buf + prefix_len,
                  sizeof(line_buf) - prefix_len, fmt, ap);
        va_end(ap);
    }

    /* 确保末尾有换行 */
    size_t total = strlen(line_buf);
    if (total + 1 < sizeof(line_buf)) {
        line_buf[total]     = '\n';
        line_buf[total + 1] = '\0';
        total++;
    }

    /* ---- 写入环形缓冲区 ---- */
    pthread_mutex_lock(&g_log.lock);

    /* 如果环形缓冲满了，丢弃最旧的一条（保证不阻塞业务） */
    ring_entry_t *entry = &g_log.ring[g_log.head];
    if (entry->len > 0) {
        fprintf(stderr, "[LOG] ring buffer full, dropping old entry\n");
    }

    memcpy(entry->data, line_buf, total);
    entry->len = total;
    g_log.head = (g_log.head + 1) % g_log.ring_size;

    /* 通知后台线程 */
    pthread_cond_signal(&g_log.cond);
    pthread_mutex_unlock(&g_log.lock);

    /* FATAL 级别 → 立即刷盘 + 终止程序 */
    if (level == LOG_FATAL) {
        log_shutdown();
        _exit(1);
    }
}

void log_shutdown(void)
{
    g_log.running = 0;

    /* 唤醒后台线程让它退出 */
    pthread_cond_signal(&g_log.cond);
    pthread_join(g_log.flush_thread, NULL);

    /* 清空残留的环形缓冲区 */
    for (int i = 0; i < g_log.ring_size; i++) {
        ring_entry_t *entry = &g_log.ring[i];
        if (entry->len > 0) {
            if (g_log.fp) {
                fwrite(entry->data, 1, entry->len, g_log.fp);
            }
            fprintf(stderr, "%.*s", entry->len, entry->data);
            entry->len = 0;
        }
    }

    if (g_log.fp) {
        fclose(g_log.fp);
        g_log.fp = NULL;
    }

    pthread_mutex_destroy(&g_log.lock);
    pthread_cond_destroy(&g_log.cond);
    free(g_log.ring);
    g_log.ring = NULL;
}
