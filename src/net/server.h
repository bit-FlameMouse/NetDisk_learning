#ifndef SERVER_H
#define SERVER_H

/** 初始化服务端：创建 socket、bind、listen、epoll */
int server_init(const char *host, int port);

/** 进入事件循环（阻塞） */
int server_event_loop(int listen_fd);

/** 清理：关闭所有连接、释放资源 */
int server_shutdown(void);

#endif
