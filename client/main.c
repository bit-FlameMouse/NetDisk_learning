/**
 * client/main.c — 客户端入口 + REPL 交互循环
 * 负责人：阿杰
 */
#include "commands/commands.h"
#include "client_protocol/client_protocol.h"
#include "client_config/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================
 * 客户端配置（全局）
 * ======================================================================== */

static client_config_t *g_cli_cfg = NULL;

/* ========================================================================
 * 别名映射表
 * ======================================================================== */
typedef struct { const char *name, *canon; } alias_t;
static const alias_t g_aliases[] = {
    {"ls","ls"},{"dir","ls"},{"ll","ll"},
    {"mkdir","mkdir"},{"rm","rm"},{"del","rm"},
    {"mv","mv"},{"rename","mv"},
    {"put","put"},{"upload","put"},
    {"get","get"},{"download","get"},
    {"stat","stat"},{"cd","cd"},{"pwd","pwd"},
    {"login","login"},{"logout","logout"},{"register","register"},
    {"whoami","whoami"},{"help","help"},
    {NULL,NULL}
};

static const char *resolve_alias(const char *cmd) {
    for (int i=0; g_aliases[i].name; i++)
        if (strcmp(cmd, g_aliases[i].name)==0) return g_aliases[i].canon;
    return NULL;
}

static void print_help(void) {
    printf("Commands: register login logout whoami\n");
    printf("          ls(ll) cd pwd mkdir rm(del) mv(rename) stat\n");
    printf("          put(upload) get(download)\n");
    printf("Aliases:  dir→ls  del→rm  upload→put  download→get  rename→mv\n");
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  -c FILE  配置文件路径（默认: client/client.conf）\n");
    printf("  -H HOST  服务端 IP（覆盖配置文件）\n");
    printf("  -p PORT  服务端端口（覆盖配置文件）\n");
    printf("  -h HOST  同 -H（兼容旧用法）\n");
    printf("  --help   显示此帮助信息\n");
}

int main(int argc, char **argv) {
    const char *config_path = "client/client.conf";
    const char *host = NULL;
    int port = 0;

    /* ---- 解析命令行参数 ---- */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-H") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0) {
            /* 兼容旧用法: -h 作为 host */
            if (i + 1 < argc && argv[i + 1][0] != '-') host = argv[++i];
            else { print_usage(argv[0]); return 0; }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* ---- 加载配置文件 ---- */
    g_cli_cfg = client_config_load(config_path);
    if (!g_cli_cfg) {
        fprintf(stderr, "Failed to load config\n");
        return 1;
    }

    /* 命令行参数覆盖配置文件值 */
    if (!host) host = g_cli_cfg->server_host;
    if (!port) port = g_cli_cfg->server_port;

    int sockfd = cli_connect(host, port);
    if (sockfd < 0) { printf("[ERROR] Cannot connect to %s:%d\n", host, port); client_config_free(g_cli_cfg); return 1; }
    printf("Connected to %s:%d\n", host, port);
    printf("Type 'help' for commands.\n\n");

    char line[1024];
    while (1) {
        printf("NetDisk> "); fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line,"\r\n")] = '\0';
        if (strlen(line)==0) continue;

        char cmd[32]={0}, a1[512]={0}, a2[512]={0};
        sscanf(line, "%31s %511s %511s", cmd, a1, a2);

        const char *canon = resolve_alias(cmd);
        if (!canon) { printf("Unknown command: %s\n", cmd); continue; }

        if (strcmp(canon,"help")==0) print_help();
        else if (strcmp(canon,"register")==0) {
            char pass[64]; printf("Password: "); fgets(pass,64,stdin); pass[strcspn(pass,"\r\n")]=0;
            cmd_register(sockfd, a1, pass);
        }
        else if (strcmp(canon,"login")==0) {
            char pass[64]; printf("Password: "); fgets(pass,64,stdin); pass[strcspn(pass,"\r\n")]=0;
            cmd_login(sockfd, a1, pass);
        }
        else if (strcmp(canon,"logout")==0) cmd_logout(sockfd);
        else if (strcmp(canon,"whoami")==0) cmd_whoami();
        else if (strcmp(canon,"ls")==0)  cmd_ls(sockfd, a1, 0);
        else if (strcmp(canon,"ll")==0)  cmd_ls(sockfd, a1, 1);
        else if (strcmp(canon,"cd")==0)  cmd_cd(sockfd, a1);
        else if (strcmp(canon,"pwd")==0) cmd_pwd();
        else if (strcmp(canon,"mkdir")==0) cmd_mkdir(sockfd, a1);
        else if (strcmp(canon,"rm")==0)  cmd_rm(sockfd, a1);
        else if (strcmp(canon,"mv")==0)  cmd_mv(sockfd, a1, a2);
        else if (strcmp(canon,"put")==0) cmd_put(sockfd, a1, a2);
        else if (strcmp(canon,"get")==0) cmd_get(sockfd, a1, a2);
        else if (strcmp(canon,"stat")==0) cmd_stat(sockfd, a1);
    }

    if (g_is_logged_in) cmd_logout(sockfd);
    cli_disconnect(sockfd);
    client_config_free(g_cli_cfg);
    return 0;
}