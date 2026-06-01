#ifndef TLV_H
#define TLV_H

#include "types.h"
#include <stddef.h>
#include <stdint.h>

/* ========================================================================
 * 编码：各类型 → TLV 二进制
 * ======================================================================== */

///@brief 写入变长二进制数据，作为写入特定类型数据的具体实现
///@param buf     输出缓冲区
///@param type    类型
///@param len     长度
///@param val     值
///@return        写入的总字节数，或负数表示错误
int tlv_write(uint8_t *buf, uint16_t type, uint16_t len, const void *val);

///@brief 写入字符串
///@param buf     输出缓冲区
///@param type    类型
///@param str     字符串
///@return        写入的总字节数，或负数表示错误
int tlv_write_str(uint8_t *buf, uint16_t type, const char *str);

///@brief 写入 uint8
///@param buf     输出缓冲区
///@param type    类型
///@param val     值
///@return        写入的总字节数，或负数表示错误
int tlv_write_u8(uint8_t *buf, uint16_t type, uint8_t val);

///@brief 写入 uint32
///@param buf     输出缓冲区
///@param type    类型
///@param val     值
///@return        写入的总字节数，或负数表示错误
int tlv_write_u32(uint8_t *buf, uint16_t type, uint32_t val);

///@brief 写入 uint64
///@param buf     输出缓冲区
///@param type    类型
///@param val     值
///@return        写入的总字节数，或负数表示错误
int tlv_write_u64(uint8_t *buf, uint16_t type, uint64_t val);

///@brief 写入列表结束标记（ENTRY_END + Length=1 + Value=0x00）
///@param buf     输出缓冲区
///@return        写入的总字节数，或负数表示错误
int tlv_write_end(uint8_t *buf);

/* ========================================================================
 * 解码：TLV 二进制 → 各类型
 * ======================================================================== */

///@brief 从 TLV 流中按 Type 查找并读取字符串 Value
///@param buf     输入缓冲区
///@param end     输入缓冲区结束位置
///@param type    类型
///@param out[out]     输出：字符串
///@param out_size[out] 输出：字符串长度
///@return        0=成功, -1=数据不足或格式错误
int tlv_get_str(const uint8_t *buf, const uint8_t *end, uint16_t type,
                char *out, size_t out_size);

///@brief 从 TLV 流中按 Type 查找并读取 uint64 Value
///@param buf     输入缓冲区
///@param end     输入缓冲区结束位置
///@param type    类型
///@param val[out]     输出：值
///@return        0=成功, -1=数据不足或格式错误
int tlv_get_u64(const uint8_t *buf, const uint8_t *end, uint16_t type,
                uint64_t *val);

///@brief 从 TLV 流中按 Type 查找并读取 uint32 Value
///@param buf     输入缓冲区
///@param end     输入缓冲区结束位置
///@param type    类型
///@param val[out]     输出：值
///@return        0=成功, -1=数据不足或格式错误
int tlv_get_u32(const uint8_t *buf, const uint8_t *end, uint16_t type,
                uint32_t *val);

///@brief 从 TLV 流中按 Type 查找并读取 uint8 Value
///@param buf     输入缓冲区
///@param end     输入缓冲区结束位置
///@param type    类型
///@param val[out]     输出：值
///@return        0=成功, -1=数据不足或格式错误
int tlv_get_u8(const uint8_t *buf, const uint8_t *end, uint16_t type,
               uint8_t *val);

///@brief 从 TLV 流中按 Type 查找并返回原始二进制指针（不复制）
///@param buf         输入缓冲区
///@param end         输入缓冲区结束位置
///@param type        类型
///@param len_out[out]     输出：数据长度
///@return            指向数据的指针，或 NULL 表示未找到
const uint8_t *tlv_get_raw(const uint8_t *buf, const uint8_t *end,
                           uint16_t type, uint16_t *len_out);

#endif
