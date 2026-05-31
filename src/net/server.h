#pragma once
#ifndef SERVER_H
#define SERVER_H
#include "base/config.h"

extern const config_t *g_config;             // 全局配置指针
extern int g_max_conns;                      // 配置决定大小
int server_init(const char *host, int port); // 初始化服务端、绑定端口

int server_event_loop(int listen_fd); // 进入事件循环

// 清理
int server_shutdown(void);
#endif
