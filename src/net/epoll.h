#pragma once
#ifndef EPOLL_H
#define EPOLL_H


int add_fd_to_epoll(int epoll_fd, int fd);

int del_fd_from_epoll(int epoll_fd, int fd);

#endif
