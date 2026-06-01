#ifndef EPOLL_H
#define EPOLL_H

#include <sys/epoll.h>

/** 添加 fd 到 epoll（ET 边缘触发模式），data.ptr = ptr */
int add_fd_to_epoll(int epoll_fd, int fd, void *ptr);

/** 从 epoll 移除 fd */
int del_fd_from_epoll(int epoll_fd, int fd);

/** 修改 fd 监听事件（切换 IN/OUT），需要传入 data.ptr 以保持连接上下文 */
int mod_fd_in_epoll(int epoll_fd, int fd, uint32_t events, void *ptr);

#endif
