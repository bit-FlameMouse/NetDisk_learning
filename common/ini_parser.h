/**
 * ini_parser.h — 通用 INI 配置文件解析器
 *
 * 通过回调函数机制，将"如何识别 section / 如何映射 key-value"的
 * 决策权交给调用方，解析引擎本身不关心具体字段。
 *
 * 服务端 common/config/ 和客户端 client/client_config/ 共用此引擎。
 */

#ifndef INI_PARSER_H
#define INI_PARSER_H

/* ========================================================================
 * 回调函数类型
 * ======================================================================== */

/**
 * Section 检测回调。
 *
 * 每遇到一个 [xxx] 行就调用一次。
 *
 * @param line  去掉了首尾空白的完整行（含中括号），如 "[server]"
 * @return      调用方自定义的 section 句柄（不透明指针）。
 *              返回 NULL 表示不识别此 section，后续 key=value 将被忽略。
 */
typedef void *(*ini_section_fn)(const char *line);

/**
 * Key-Value 应用回调。
 *
 * 每个有效的 "key = value" 行调用一次。
 *
 * @param section  当前所处的 section 句柄（由 ini_section_fn 返回）
 *                 NULL 表示尚未进入任何 section
 * @param key      键名
 * @param value    值字符串
 */
typedef void (*ini_kv_fn)(void *section, const char *key, const char *value);

/* ========================================================================
 * 公开接口
 * ======================================================================== */

/**
 * 解析 INI 格式配置文件，逐行读取并通过回调通知调用方。
 *
 * 规则：
 *   - 空行和以 '#' 开头的行 → 忽略
 *   - [xxx] 行 → 调用 on_sec，返回值作为后续 kv 的 section 参数
 *   - key = value 行 → 调用 on_kv
 *   - 文件不存在 → 返回 -1，调用方应使用默认值
 *
 * @param path   配置文件路径
 * @param on_sec section 检测回调（可为 NULL，此时所有 section 都被忽略）
 * @param on_kv  key-value 应用回调（可为 NULL，此时只解析不应用）
 * @return       0 = 成功, -1 = 文件无法打开
 */
int ini_parse(const char *path, ini_section_fn on_sec, ini_kv_fn on_kv);

#endif /* INI_PARSER_H */
