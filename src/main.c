#include "base/config.h"
#include "base/thread_pool.h"
#include "net/server.h"
#include <stdio.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <unistd.h>

int pipe_fd[2];

void handler(int signum) {
  printf("Child process received signal %d\n", signum);
  write(pipe_fd[1], "1", 1);
}
int main() {
  pipe(pipe_fd);
  if (fork() != 0) {    // 父进程
    signal(2, handler); // 注册中断信号为handler
  }

  // 父进程是前台进程
  setpgid(0, 0); // 设置进程组ID，使子进程成为后台进程
  config_t *config = config_load("config/netdisk.conf");

  thread_pool_t *pool = thread_pool_create(4);
  if (pool != NULL) {
    thread_pool_destroy(pool);
  }

  int listen_fd = server_init(config->server_host, config->server_port);

  server_event_loop(listen_fd); // 进入事件循环


  // 清理
  server_shutdown();
  close(listen_fd);  // 关闭监听文件描述符
  config_free(config); // 释放配置
  return 0;

  return 0;
}
