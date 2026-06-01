/**
 * timer.c — 时间轮实现
 *
 * 60 槽位循环时间轮。
 * - 添加/删除/刷新: O(1)
 * - 到期处理: O(k), k = 该槽位链表长度
 * - 自旋锁保护临界区（链表操作 <100ns）
 */
#include "timer.h"
#include "../base/log/log.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 时间轮节点（每个连接一个） */
typedef struct tw_node {
    conn_t         *conn;       /* 所属连接 */
    int             rotation;   /* 剩余圈数 */
    timer_callback_t callback;  /* 超时回调 */
    struct tw_node *next;       /* 同槽位链表 */
} tw_node_t;

/* 时间轮 */
typedef struct {
    tw_node_t      **slots;     /* slot_count 个链表头 */
    int              slot_count;
    int              current;    /* 当前指针 0..slot_count-1 */
    int              tick_count; /* 总 tick 计数 */
    pthread_spinlock_t lock;
} time_wheel_t;

static time_wheel_t g_tw;

/* 默认超时回调：关闭连接 */
static void timeout_close(conn_t *conn)
{
    log_debug("timer: fd=%d idle timeout, closing", conn->fd);
    /* 标记连接为待关闭，由主循环执行实际 close */
    conn->state = -1;
}

int timer_init(int slot_count)
{
    memset(&g_tw, 0, sizeof(g_tw));
    g_tw.slot_count = slot_count > 0 ? slot_count : 60;
    g_tw.slots = calloc((size_t)g_tw.slot_count, sizeof(tw_node_t *));
    if (!g_tw.slots) return -1;
    pthread_spin_init(&g_tw.lock, PTHREAD_PROCESS_PRIVATE);
    log_info("Timer: %d slots initialized", g_tw.slot_count);
    return 0;
}

void timer_tick(void)
{
    pthread_spin_lock(&g_tw.lock);

    g_tw.current = (g_tw.current + 1) % g_tw.slot_count;
    g_tw.tick_count++;

    tw_node_t *prev = NULL;
    tw_node_t *node = g_tw.slots[g_tw.current];

    while (node) {
        tw_node_t *next = node->next;
        if (node->rotation > 0) {
            node->rotation--;
            prev = node;
        } else {
            /* 到期：从链表移除并回调 */
            if (prev) prev->next = next;
            else      g_tw.slots[g_tw.current] = next;

            if (node->callback && node->conn) {
                node->callback(node->conn);
            }
            free(node);
        }
        node = next;
    }

    pthread_spin_unlock(&g_tw.lock);
}

int timer_add(conn_t *conn, int timeout_sec)
{
    if (!conn || timeout_sec <= 0) return -1;

    /* 先删除旧节点 */
    timer_del(conn);

    tw_node_t *node = calloc(1, sizeof(tw_node_t));
    if (!node) return -1;

    node->conn     = conn;
    node->callback = timeout_close;

    int total_ticks = timeout_sec;
    int slot = (g_tw.current + total_ticks) % g_tw.slot_count;
    node->rotation = (g_tw.current + total_ticks) / g_tw.slot_count;

    pthread_spin_lock(&g_tw.lock);
    node->next = g_tw.slots[slot];
    g_tw.slots[slot] = node;
    conn->tw_node = node;
    pthread_spin_unlock(&g_tw.lock);

    return 0;
}

void timer_del(conn_t *conn)
{
    if (!conn || !conn->tw_node) return;

    tw_node_t *target = (tw_node_t *)conn->tw_node;

    pthread_spin_lock(&g_tw.lock);

    /* 找到 target 所在的槽位并移除 */
    for (int i = 0; i < g_tw.slot_count; i++) {
        tw_node_t *prev = NULL;
        tw_node_t *node = g_tw.slots[i];
        while (node) {
            if (node == target) {
                if (prev) prev->next = node->next;
                else      g_tw.slots[i] = node->next;
                free(node);
                conn->tw_node = NULL;
                pthread_spin_unlock(&g_tw.lock);
                return;
            }
            prev = node;
            node = node->next;
        }
    }

    pthread_spin_unlock(&g_tw.lock);
}

void timer_destroy(void)
{
    pthread_spin_lock(&g_tw.lock);
    for (int i = 0; i < g_tw.slot_count; i++) {
        tw_node_t *node = g_tw.slots[i];
        while (node) {
            tw_node_t *next = node->next;
            free(node);
            node = next;
        }
        g_tw.slots[i] = NULL;
    }
    pthread_spin_unlock(&g_tw.lock);

    pthread_spin_destroy(&g_tw.lock);
    free(g_tw.slots);
    memset(&g_tw, 0, sizeof(g_tw));
    log_info("Timer destroyed");
}
