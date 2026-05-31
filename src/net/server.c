#include "server.h"
#include "epoll.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int g_epoll_fd = -1;

int server_init(const char *host, int port) {
  int listen_fd;
  struct sockaddr_in addr; // 服务器地址结构体
  int opt = 1;
  if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return -1;
  }
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, host, &addr.sin_addr);
  addr.sin_port = htons(port);
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    close(listen_fd);
    return -1;
  }
  if (listen(listen_fd, SOMAXCONN) == -1) {
    perror("listen");
    close(listen_fd);
    return -1;
  }

  g_epoll_fd = epoll_create(1);
  if (g_epoll_fd == -1) {
    perror("epoll_create1");
    close(listen_fd);
    return -1;
  }

  if (add_fd_to_epoll(g_epoll_fd, listen_fd) == -1) {
    perror("add_fd_to_epoll");
    close(listen_fd);
    close(g_epoll_fd);
    return -1;
  }

  return listen_fd;
}
