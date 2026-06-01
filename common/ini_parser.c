/**
 * ini_parser.c — 通用 INI 解析引擎实现
 *
 * 提取自 common/config/config.c 和 client/client_config/config.c
 * 中完全重复的 trim / parse_kv / 逐行读取循环。
 */

#include "ini_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* 单行最大长度 */
#define LINE_MAX 512

/* ========================================================================
 * 内部工具 — trim（去首尾空白）
 * ======================================================================== */

static char *trim(char *s)
{
    /* 去尾部空白 */
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) *end-- = '\0';

    /* 去首部空白 */
    while (isspace((unsigned char)*s)) s++;
    return s;
}

/* ========================================================================
 * 内部工具 — parse_kv（解析 "key = value"）
 * ======================================================================== */

static int parse_kv(const char *line, char *key, size_t ksz,
                    char *value, size_t vsz)
{
    const char *eq = strchr(line, '=');
    if (!eq) return 0;

    /* 提取 key */
    size_t klen = eq - line;
    if (klen >= ksz) klen = ksz - 1;
    memcpy(key, line, klen);
    key[klen] = '\0';
    strcpy(key, trim(key));
    if (strlen(key) == 0) return 0;

    /* 提取 value */
    strncpy(value, eq + 1, vsz - 1);
    value[vsz - 1] = '\0';
    strcpy(value, trim(value));
    return 1;
}

/* ========================================================================
 * 公开接口 — ini_parse
 * ======================================================================== */

int ini_parse(const char *path, ini_section_fn on_sec, ini_kv_fn on_kv)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "ini_parse: cannot open %s\n", path);
        return -1;
    }

    char   line[LINE_MAX];
    void  *current_section = NULL;  /* 当前 section 句柄 */
    int    lineno = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;

        /* 去掉末尾换行符 */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        char *t = trim(line);

        /* 跳过空行和注释 */
        if (t[0] == '\0' || t[0] == '#') continue;

        /* [section] 行 */
        if (t[0] == '[') {
            if (on_sec) {
                void *sec = on_sec(t);
                if (sec) {
                    current_section = sec;
                } else {
                    fprintf(stderr, "ini_parse: line %d: unknown section '%s'\n",
                            lineno, t);
                    current_section = NULL;
                }
            }
            continue;
        }

        /* key = value 行 */
        if (on_kv) {
            char key[128], value[256];
            if (parse_kv(t, key, sizeof(key), value, sizeof(value))) {
                on_kv(current_section, key, value);
            } else {
                fprintf(stderr, "ini_parse: line %d: invalid syntax '%s'\n",
                        lineno, t);
            }
        }
    }

    fclose(fp);
    return 0;
}
