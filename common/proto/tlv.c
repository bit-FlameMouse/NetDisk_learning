
#include "tlv.h"
#include <string.h>
#include <arpa/inet.h>

/* ========================================================================
 * 编码工具
 * ======================================================================== */

int tlv_write(uint8_t *buf, uint16_t type, uint16_t len, const void *val)
{
    buf[0] = (uint8_t)type;                     /* Type   1B */
    uint16_t be_len = htons(len);
    memcpy(buf + 1, &be_len, 2);                /* Length 2B BE */
    if (len > 0 && val) memcpy(buf + 3, val, len); /* Value */
    return 3 + len;
}

int tlv_write_str(uint8_t *buf, uint16_t type, const char *str)
{
    uint16_t len = str ? (uint16_t)strlen(str) : 0;
    return tlv_write(buf, type, len, str);
}

int tlv_write_u8(uint8_t *buf, uint16_t type, uint8_t val)
{
    return tlv_write(buf, type, 1, &val);
}

int tlv_write_u32(uint8_t *buf, uint16_t type, uint32_t val)
{
    uint32_t be = htonl(val);
    return tlv_write(buf, type, 4, &be);
}

int tlv_write_u64(uint8_t *buf, uint16_t type, uint64_t val)
{
    /* 手动大端编码（htonll 非 POSIX，用移位代替） */
    uint8_t be[8];
    be[0] = (val >> 56) & 0xFF;
    be[1] = (val >> 48) & 0xFF;
    be[2] = (val >> 40) & 0xFF;
    be[3] = (val >> 32) & 0xFF;
    be[4] = (val >> 24) & 0xFF;
    be[5] = (val >> 16) & 0xFF;
    be[6] = (val >> 8)  & 0xFF;
    be[7] =  val        & 0xFF;
    return tlv_write(buf, type, 8, be);
}

int tlv_write_end(uint8_t *buf)
{
    /* ENTRY_END + Length=1 + Value=0x00 = 列表终止标记 */
    buf[0] = TLV_ENTRY_END;
    buf[1] = 0;
    buf[2] = 1;   /* Length = 1 */
    buf[3] = 0;   /* Value = 0x00 */
    return 4;
}

/* ========================================================================
 * 解码工具
 * ======================================================================== */

///@brief 读取一个 TLV 头（Type + Length），不消费 Value。
///@param buf   输入缓冲区（调用后 *buf 前进到 Value 起始位置）
///@param type  输出：Type
///@param len   输出：Value 长度
///@return      0=成功, -1=数据不足或格式错误
int tlv_read_header(const uint8_t **buf, const uint8_t *end,
                    uint16_t *type, uint16_t *len)
{
    if (*buf + 3 > end) return -1;  /* 至少需要 Type(1) + Length(2) */

    *type = (*buf)[0];
    uint16_t be_len;
    memcpy(&be_len, *buf + 1, 2);
    *len = ntohs(be_len);

    *buf += 3;  /* 前进到 Value 起始 */
    if (*buf + *len > end) return -1;  /* Value 长度超出边界 */

    return 0;
}


///@brief 在 TLV 流中查找指定类型的数据
///@param buf         输入缓冲区
///@param end         输入缓冲区结束位置
///@param target_type 要查找的类型
///@param len_out     输出：数据长度
///@return            指向数据的指针，或 NULL 表示未找到
static const uint8_t *tlv_find(const uint8_t *buf, const uint8_t *end,
                               uint16_t target_type, uint16_t *len_out)
{
    while (buf < end) {
        uint16_t type, len;
        if (tlv_read_header(&buf, end, &type, &len) < 0) return NULL;
        if (type == target_type) {
            if (len_out) *len_out = len;
            return buf;
        }
        buf += len;  /* 跳过不匹配的 Value */
    }
    return NULL;
}



int tlv_get_str(const uint8_t *buf, const uint8_t *end,
                uint16_t type, char *out, size_t out_size)
{
    uint16_t len;
    const uint8_t *val = tlv_find(buf, end, type, &len);
    if (val == NULL) return -1;

    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, val, copy);
    out[copy] = '\0';
    return (int)copy;
}

int tlv_get_u64(const uint8_t *buf, const uint8_t *end,
                uint16_t type, uint64_t *val)
{
    uint16_t len;
    const uint8_t *raw = tlv_find(buf, end, type, &len);
    if (raw == NULL || len != 8) return -1;

    *val = ((uint64_t)raw[0] << 56) | ((uint64_t)raw[1] << 48) |
           ((uint64_t)raw[2] << 40) | ((uint64_t)raw[3] << 32) |
           ((uint64_t)raw[4] << 24) | ((uint64_t)raw[5] << 16) |
           ((uint64_t)raw[6] << 8)  |  (uint64_t)raw[7];
    return 8;
}

int tlv_get_u32(const uint8_t *buf, const uint8_t *end,
                uint16_t type, uint32_t *val)
{
    uint16_t len;
    const uint8_t *raw = tlv_find(buf, end, type, &len);
    if (raw == NULL || len != 4) return -1;

    uint32_t be;
    memcpy(&be, raw, 4);
    *val = ntohl(be);
    return 4;
}

int tlv_get_u8(const uint8_t *buf, const uint8_t *end,
               uint16_t type, uint8_t *val)
{
    uint16_t len;
    const uint8_t *raw = tlv_find(buf, end, type, &len);
    if (raw == NULL || len != 1) return -1;

    *val = raw[0];
    return 1;
}

const uint8_t *tlv_get_raw(const uint8_t *buf, const uint8_t *end,
                           uint16_t type, uint16_t *len_out)
{
    return tlv_find(buf, end, type, len_out);
}

