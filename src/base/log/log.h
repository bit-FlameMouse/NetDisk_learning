#pragma once
#ifndef LOG_H
#define LOG_H

/* ========================================================================
 * 日志级别
 * ======================================================================== */

#define LOG_DEBUG 0 // debug
#define LOG_INFO 1  // info
#define LOG_WARN 2  // warn
#define LOG_ERROR 3 // error
#define LOG_FATAL 4 // fatal

/* ========================================================================
 * 公开接口
 * ======================================================================== */

/**
 * 初始化日志模块。
 *
 * @param file_path   日志文件路径（NULL = 仅输出到 stderr）
 * @param level       最低记录级别（LOG_DEBUG ~ LOG_FATAL）
 * @param max_size    单文件最大字节数（0 = 不轮转）
 * @param backups     保留的历史文件数
 */
void log_init(const char *file_path, int level, int max_size, int backups);

/**
 * 内部写入函数（不要直接调用，使用宏）。
 *
 * @param level   日志级别（LOG_DEBUG ~ LOG_FATAL）
 * @param file    文件名
 * @param line    行号
 * @param fmt     格式化字符串
 * @param ...     可变参数
 */
void log_write(int level, const char *file, int line, const char *fmt, ...);

/**
 * 关闭日志模块：等待缓冲清空、关闭文件。
 */
void log_shutdown(void);

/* ========================================================================
 * 便捷宏（使用者只需关心这几个）
 * ======================================================================== */

#define log_debug(fmt, ...)                                                    \
  log_write(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)                                                     \
  log_write(LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)                                                     \
  log_write(LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...)                                                    \
  log_write(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define log_fatal(fmt, ...)                                                    \
  log_write(LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
