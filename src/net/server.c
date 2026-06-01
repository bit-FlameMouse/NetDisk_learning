/**
 * server.c — 服务端 Reactor 事件循环
 *
 * epoll ET 边缘触发 + 线程池 + 时间轮。
 * 主线程纯 I/O：accept / read frame / write response。
 * 业务逻辑通过线程池异步处理。
 */
#include "server.h"
#include "epoll.h"
#include "timer.h"
#include "protocol.h"
#include "../route/dispatcher.h"
#include "../global.h"
#include "../base/log/log.h"
#include "../base/thread_pool/thread_pool.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_epoll_fd = -1;
static int g_event_fd = -1;  /* Worker 通知主线程：有数据可写 */
static int g_running  = 1;

/* 连接链表（用于清理） */
static conn_t *g_conns = NULL;
static int     g_conn_count = 0;

/* ========================================================================
 * 连接管理
 * ======================================================================== */

static conn_t *conn_create(int fd)
{
    conn_t *c = calloc(1, sizeof(conn_t));
    if (!c) return NULL;
    c->fd          = fd;
    c->state       = CONN_HANDSHAKE;
    c->frame_state = FRAME_MAGIC1;
    c->next        = g_conns;
    g_conns        = c;
    g_conn_count++;
    return c;
}

static void conn_destroy(conn_t *c)
{
    if (!c) return;

    timer_del(c);
    del_fd_from_epoll(g_epoll_fd, c->fd);
    free(c->payload_buf);
    close(c->fd);

    /* 从链表移除 */
    conn_t **pp = &g_conns;
    while (*pp) {
        if (*pp == c) { *pp = c->next; break; }
        pp = &(*pp)->next;
    }
    g_conn_count--;

    log_debug("server: conn fd=%d closed (%d active)", c->fd, g_conn_count);
    free(c);
}

/* ========================================================================
 * 任务封装（投递给线程池）
 * ======================================================================== */

typedef struct {
    conn_t  *conn;
    frame_t  frame;    /* 帧头 + payload 指针（payload 堆分配） */
} handler_task_t;

static void handler_task_func(void *arg)
{
    handler_task_t *t = (handler_task_t *)arg;

    /* 调用命令分发器 */
    dispatch(t->conn, &t->frame);

    /* 通知主线程：此连接有数据待发送 */
    eventfd_write(g_event_fd, 1);

    /* 释放帧载荷（由 protocol_feed malloc，dispatch 后不再需要） */
    free(t->frame.payload);
    free(t);
}

static void submit_frame(conn_t *conn)
{
    frame_t *frame = protocol_get_frame(conn);
    if (!frame) return;

    /* 重置空闲超时 */
    timer_add(conn, 60);

    /* 创建任务的帧拷贝（堆分配 payload） */
    handler_task_t *task = malloc(sizeof(handler_task_t));
    if (!task) { frame_free(frame); return; }

    task->conn  = conn;
    task->frame = *frame;  /* 拷贝帧头 */
    /* 转移 payload 所有权给 task */
    task->frame.payload = frame->payload;
    frame->payload = NULL;

    task_t t = { .func = handler_task_func, .arg = task };
    thread_pool_submit(g_thread_pool, t);
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

int server_init(const char *host, int port)
{
    int listen_fd;
    struct sockaddr_in addr;
    int opt = 1;

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    inet_pton(AF_INET, host, &addr.sin_addr);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind"); close(listen_fd); return -1;
    }
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen"); close(listen_fd); return -1;
    }

    g_epoll_fd = epoll_create(1);
    if (g_epoll_fd == -1) {
        perror("epoll_create"); close(listen_fd); return -1;
    }

    /* 注册监听 socket */
    {
        struct epoll_event lev;
        lev.events  = EPOLLIN | EPOLLET;
        lev.data.fd = listen_fd;
        if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, listen_fd, &lev) == -1) {
            perror("epoll_ctl: listen"); close(listen_fd); close(g_epoll_fd); return -1;
        }
    }

    /* 创建 eventfd：Worker 通知主线程有数据可写 */
    g_event_fd = eventfd(0, EFD_NONBLOCK);
    if (g_event_fd == -1) {
        perror("eventfd"); close(listen_fd); close(g_epoll_fd); return -1;
    }

    struct epoll_event ev;
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = g_event_fd;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, g_event_fd, &ev);

    log_info("Server listening on %s:%d (fd=%d)", host, port, listen_fd);
    return listen_fd;
}

/* ========================================================================
 * 事件循环
 * ======================================================================== */

#define MAX_EVENTS 1024

int server_event_loop(int listen_fd)
{
    struct epoll_event events[MAX_EVENTS];

    log_info("Server event loop started");

    while (g_running) {
        int nfds = epoll_wait(g_epoll_fd, events, MAX_EVENTS, 1000);

        /* ---- 时间轮推进 ---- */
        timer_tick();

        /* ---- 清理标记为关闭的连接 ---- */
        {
            conn_t *c = g_conns;
            while (c) {
                conn_t *next = c->next;
                if (c->state == -1) conn_destroy(c);
                c = next;
            }
        }

        if (nfds < 0) {
            if (errno == EINTR) continue;
            log_error("epoll_wait: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            /* ---- 新连接 (listen_fd 通过 data.fd 获取) ---- */
            if (fd == listen_fd) {
                while (1) {
                    struct sockaddr_in caddr;
                    socklen_t clen = sizeof(caddr);
                    int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        log_error("accept: %s", strerror(errno));
                        break;
                    }

                    /* 检查连接数限制 */
                    if (g_conn_count >= MAX_CONNECTIONS) {
                        log_warn("server: max connections reached, rejecting");
                        close(cfd);
                        continue;
                    }

                    conn_t *conn = conn_create(cfd);
                    if (!conn) { close(cfd); continue; }

                    /* 设置客户端 fd 为非阻塞，注册到 epoll */
                    int flags = fcntl(cfd, F_GETFL, 0);
                    fcntl(cfd, F_SETFL, flags | O_NONBLOCK);

                    struct epoll_event cev;
                    cev.events   = EPOLLIN | EPOLLET;
                    cev.data.ptr = conn;
                    epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, cfd, &cev);

                    /* 注册到时间轮（60 秒空闲超时） */
                    timer_add(conn, 60);

                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &caddr.sin_addr, ip, sizeof(ip));
                    log_debug("server: accept fd=%d from %s:%d",
                              cfd, ip, ntohs(caddr.sin_port));
                }
                continue;
            }

            /* ---- eventfd：Worker 通知有数据待发送 ---- */
            if (fd == g_event_fd) {
                eventfd_t val;
                eventfd_read(g_event_fd, &val);

                /* 遍历所有连接，检查是否有待发送数据 */
                conn_t *c = g_conns;
                while (c) {
                    if (c->send_len > 0) {
                        mod_fd_in_epoll(g_epoll_fd, c->fd,
                                        EPOLLIN | EPOLLOUT, c);
                    }
                    c = c->next;
                }
                continue;
            }

            /* ---- 获取连接上下文 ---- */
            conn_t *conn = (conn_t *)events[i].data.ptr;
            if (!conn) continue;

            /* ---- 错误/挂断 ---- */
            if (ev & (EPOLLERR | EPOLLHUP)) {
                conn->state = -1;
                continue;
            }

            /* ---- 可写：发送响应 ---- */
            if (ev & EPOLLOUT) {
                while (conn->send_len > 0) {
                    /* 环形缓冲区：从 tail 开始读 */
                    int tail = conn->send_tail;
                    int avail;
                    if (conn->send_head >= tail) {
                        avail = conn->send_head - tail;
                    } else {
                        avail = CONN_BUF_SIZE - tail;
                    }
                    if (avail <= 0) break;
                    if (avail > conn->send_len) avail = conn->send_len;

                    ssize_t n = write(conn->fd, conn->send_buf + tail, (size_t)avail);
                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            /* 写缓冲区满，等待下次 EPOLLOUT */
                            break;
                        }
                        conn->state = -1;
                        break;
                    }
                    conn->send_tail = (conn->send_tail + n) % CONN_BUF_SIZE;
                    conn->send_len -= (int)n;
                }

                /* 全部写完 → 切回 EPOLLIN */
                if (conn->send_len == 0 && conn->state != -1) {
                    mod_fd_in_epoll(g_epoll_fd, conn->fd, EPOLLIN, conn);
                }
            }

            /* ---- 可读：接收数据 ---- */
            if (ev & EPOLLIN) {
                uint8_t buf[8192];
                int frames_done = 0;

                while (1) {
                    ssize_t n = read(conn->fd, buf, sizeof(buf));
                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        conn->state = -1;
                        break;
                    }
                    if (n == 0) {
                        /* 对端关闭 */
                        conn->state = -1;
                        break;
                    }

                    frames_done += protocol_feed(conn, buf, (int)n);
                }

                /* 处理完成的帧 */
                for (int f = 0; f < frames_done; f++) {
                    submit_frame(conn);
                }
            }
        }
    }

    log_info("Server event loop stopped");
    return 0;
}

/* ========================================================================
 * 清理
 * ======================================================================== */

int server_shutdown(void)
{
    g_running = 0;

    /* 关闭所有客户端连接 */
    conn_t *c = g_conns;
    while (c) {
        conn_t *next = c->next;
        shutdown(c->fd, SHUT_RDWR);
        close(c->fd);
        free(c->payload_buf);
        free(c);
        c = next;
    }
    g_conns = NULL;
    g_conn_count = 0;

    if (g_event_fd >= 0) {
        close(g_event_fd);
        g_event_fd = -1;
    }
    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }

    log_info("Server shutdown complete");
    return 0;
}
