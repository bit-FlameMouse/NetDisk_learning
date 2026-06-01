#include "epoll.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int add_fd_to_epoll(int epoll_fd, int fd, void *ptr)
{
    /* 设置非阻塞 */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct epoll_event event;
    event.events   = EPOLLIN | EPOLLET;  /* ET 边缘触发 */
    event.data.ptr = ptr;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl: add");
        return -1;
    }
    return 0;
}

int del_fd_from_epoll(int epoll_fd, int fd)
{
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl: del");
        return -1;
    }
    return 0;
}

int mod_fd_in_epoll(int epoll_fd, int fd, uint32_t events, void *ptr)
{
    struct epoll_event event;
    event.events   = events | EPOLLET;
    event.data.ptr = ptr;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == -1) {
        perror("epoll_ctl: mod");
        return -1;
    }
    return 0;
}
