/**
 * commands.h — 客户端命令声明
 *
 * 所有 CLI 命令的实现都在 commands.c 中。
 * 全局会话状态也在此声明。
 */

#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

/* ========================================================================
 * 认证命令
 * ======================================================================== */

int cmd_register(int sockfd, const char *user, const char *pass);
int cmd_login(int sockfd, const char *user, const char *pass);
int cmd_logout(int sockfd);
int cmd_whoami(void);

/* ========================================================================
 * 目录浏览
 * ======================================================================== */

int cmd_ls(int sockfd, const char *path, int detail);
int cmd_cd(int sockfd, const char *path);
int cmd_pwd(void);
int cmd_stat(int sockfd, const char *path);

/* ========================================================================
 * 文件操作
 * ======================================================================== */

int cmd_mkdir(int sockfd, const char *path);
int cmd_rm(int sockfd, const char *path);
int cmd_mv(int sockfd, const char *src, const char *dst);
int cmd_put(int sockfd, const char *local, const char *remote);
int cmd_get(int sockfd, const char *remote, const char *local);

/* ========================================================================
 * 全局会话状态
 * ======================================================================== */

extern char g_cwd[512];
extern char g_username[64];
extern int  g_is_logged_in;

#endif /* CLIENT_COMMANDS_H */
