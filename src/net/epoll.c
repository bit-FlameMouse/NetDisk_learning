#include "epoll.h"
#include <stdio.h>
#include <sys/epoll.h>

int add_fd_to_epoll(int epoll_fd, int fd){
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl: add");
        return -1;
    }
    return 0;
}

int del_fd_from_epoll(int epoll_fd, int fd){
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl: del");
        return -1;
    }
    return 0;
}
