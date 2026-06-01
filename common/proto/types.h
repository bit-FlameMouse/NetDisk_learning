/**

 * 这是整个项目的"宪法"——所有模块共享的常量、枚举、结构体
 * 都定义在这里。修改本文件需要通知全员 + PR Review。
 */

#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* ========================================================================
 * 一、NDP-Lite 协议帧格式常量
 * ======================================================================== */

#define NDP_MAGIC1 0x4E /* 'N' */
#define NDP_MAGIC2 0x44 /* 'D' */
#define NDP_VERSION 0x01
#define NDP_HEADER_SIZE 8           /* 帧头固定 8 字节 */
#define NDP_MAX_PAYLOAD (64 * 1024) /* 单帧载荷上限 64KB */

/* ========================================================================
 * 二、FLAGS 位掩码
 * ======================================================================== */

#define FLAG_STREAMING 0x01   /* 流式传输：后面还有数据帧 */
#define FLAG_IS_RESPONSE 0x02 /* 标记为响应帧 */

/* ========================================================================
 * 三、命令码（22 个有效 + 1 保留）
 *
 * 编码规则：
 *   0x02-0x08   → 认证相关
 *   0x10-0x1F   → 文件操作（需要 JWT 认证）
 *   0x20-0x2F   → 高级操作（断点续传等）
 *   0x80-0x8F   → 服务端响应码
 * ======================================================================== */

enum {
  /* -- 认证命令（客户端 ↔ 服务端） -- */
  /*客户端与服务端的通信*/
  CMD_AUTH = 0x02,     /* 登录请求（客户端 → 服务端） */
  CMD_AUTH_OK = 0x03,  /* 登录成功（服务端 → 客户端） */
  CMD_AUTH_ERR = 0x04, /* 登录失败（服务端 → 客户端） */
  CMD_BYE = 0x07,      /* 断开连接 */
  CMD_REGISTER = 0x08, /* 注册请求（客户端 → 服务端） */

  /* -- 文件操作命令（客户端 → 服务端） -- */
  /* 文件操作指令 */
  CMD_LS = 0x10,    /* 列出目录 */
  CMD_CD = 0x11,    /* 切换目录 */
  CMD_MKDIR = 0x12, /* 创建目录 */
  CMD_RM = 0x13,    /* 删除文件/目录 */
  CMD_MV = 0x14,    /* 移动/重命名 */
  CMD_PUT = 0x15,   /* 上传文件 */
  CMD_GET = 0x16,   /* 下载文件 */
  CMD_STAT = 0x17,  /* 查看文件信息 */

  /* -- 高级命令（客户端内部自动选择，用户不直接调用） -- */
  CMD_RESUME_PUT = 0x20, /* 上传断点续传（客户端检测到未完成会话时自动切换） */
  CMD_RESUME_GET = 0x21, /* 下载断点续传（客户端检测到部分文件时自动切换） */

  /* -- 服务端响应码 -- */
  CMD_OK = 0x80,         /* 操作成功 */
  CMD_ERR = 0x81,        /* 通用错误 */
  CMD_ERR_AUTH = 0x82,   /* 认证失败 */
  CMD_ERR_EXISTS = 0x83, /* 文件已存在 */
  CMD_ERR_NOTFND = 0x84, /* 文件/路径不存在 */
  CMD_ERR_QUOTA = 0x85,  /* 配额超限 */
  CMD_ERR_ACCESS = 0x86, /* 权限不足 */
  CMD_ERR_BUSY = 0x87,   /* 预留：服务繁忙 */
  CMD_ERR_RANGE = 0x88,  /* 偏移量非法 */
};

/* ========================================================================
 * 四、TLV Type 常量（20 种）
 * ======================================================================== */

enum {
  TLV_PATH = 0x01,       /* 路径字符串 */
  TLV_FILENAME = 0x02,   /* 文件名 */
  TLV_FILE_SIZE = 0x03,  /* 文件大小（uint64） */
  TLV_FILE_HASH = 0x04,  /* SHA256 哈希（32 字节） */
  TLV_MIME_TYPE = 0x05,  /* MIME 类型字符串 */
  TLV_TOKEN = 0x06,      /* JWT 令牌字符串 */
  TLV_USERNAME = 0x07,   /* 用户名 */
  TLV_PASSWORD = 0x08,   /* 密码 */
  TLV_ERROR_MSG = 0x09,  /* 错误消息文本 */
  TLV_FILE_ID = 0x0A,    /* 文件 ID（uint64） */
  TLV_PARENT_ID = 0x0B,  /* 父目录 ID（uint64） */
  TLV_OFFSET = 0x0C,     /* 偏移量（uint64） */
  TLV_CHUNK_SEQ = 0x0D,  /* 分片序号（uint32） */
  TLV_CHUNK_DATA = 0x0E, /* 分片数据（变长二进制） */
  TLV_CRC32 = 0x0F,      /* CRC32 校验值（uint32） */
  TLV_ACTION = 0x10,     /* 操作类型（uint8） */
  TLV_UPLOAD_ID = 0x11,  /* 上传会话 ID（uint64） */
  TLV_MSG = 0x12,        /* 消息文本 */
  TLV_ENTRY_TYPE = 0x13, /* 目录项类型（0=dir, 1=file） */
  TLV_BITMAP = 0x14, /* 分片位图（变长二进制，上传续传状态查询） */

  /* -- 边界标记（不承载实际数据） -- */
  TLV_ENTRY_START = 0xFE, /* 一个目录项的起始 */
  TLV_ENTRY_END = 0xFF,   /* 一个目录项的结束 */
};

/* ========================================================================
 * 五、连接状态枚举
 * ======================================================================== */

typedef enum {
  CONN_HANDSHAKE = 0, /* 完成 handshake，刚 accept，等待认证 */
  CONN_AUTHED = 1,    /* 已通过 JWT 认证，可以执行文件操作 */
} conn_state_t;

/* ========================================================================
 * 六、NDP 帧头结构体（8 字节，紧凑布局）
 * ======================================================================== */

#pragma pack(push, 1)
typedef struct {
  uint8_t magic[2];     /* 0-1:  'N', 'D' */
  uint8_t version;      /* 2:    协议版本 0x01 */
  uint8_t cmd;          /* 3:    命令码 */
  uint8_t flags;        /* 4:    FLAG_STREAMING | FLAG_IS_RESPONSE */
  uint8_t seq;          /* 5:    序列号（请求-响应匹配） */
  uint16_t payload_len; /* 6-7:  TLV 载荷长度（网络字节序，大端） */
} frame_header_t;
#pragma pack(pop)

/* 编译期断言：确保帧头正好 8 字节 */
#ifdef __GNUC__
_Static_assert(sizeof(frame_header_t) == 8, "frame_header_t must be 8 bytes");
#endif

/* ========================================================================
 * 七、NPD 帧完整结构（运行期）
 * ======================================================================== */

typedef struct {
  frame_header_t hdr;        /* 帧头 */
  uint8_t *payload;          /* TLV 载荷数据（堆分配） */
  uint32_t payload_capacity; /* 载荷缓冲区容量 */
} frame_t;

/* ========================================================================
 * 八、VFS 节点（虚拟文件系统的基本单元）
 * ======================================================================== */

typedef struct {
  uint64_t id;           /* 主键（数据库自增） */
  uint64_t parent_id;    /* 父目录 ID（0 = 根目录） */
  char name[256];        /* 文件名或目录名 */
  uint8_t node_type;     /* 0 = 目录, 1 = 文件 */
  uint64_t file_size;    /* 文件大小（字节；目录为 0） */
  char content_hash[65]; /* SHA256 哈希（十六进制字符串） */
  char mime_type[128];   /* MIME 类型（目录为空串） */
  time_t created_at;     /* 创建时间 */
  time_t updated_at;     /* 最后修改时间 */
  int owner_id;          /* 所有者用户 ID */
} vfs_node_t;

/* ========================================================================
 * 九、上传会话（断点续传用）
 * ======================================================================== */

typedef struct {
  uint64_t upload_id;    /* 上传会话 ID */
  uint64_t user_id;      /* 上传者 */
  uint64_t parent_id;    /* 目标父目录 */
  char filename[256];    /* 目标文件名 */
  uint64_t file_size;    /* 文件总大小 */
  char content_hash[65]; /* 期望的 SHA256 */
  char mime_type[128];   /* MIME 类型 */
  int total_chunks;      /* 总分片数 */
  int received_chunks;   /* 已接收分片数 */
  uint8_t *chunk_bitmap; /* 分片位图（VARBINARY 转内存） */
  time_t created_at;     /* 会话创建时间 */
  time_t expires_at;     /* 会话过期时间 */
} upload_session_t;

/* ========================================================================
 * 十、用户信息
 * ======================================================================== */

typedef struct {
  uint64_t id;
  char username[64];
  char password_hash[129]; /* SHA256(password + salt) */
  char salt[65];           /* 32 字节随机 salt 的十六进制 */
  uint64_t quota_bytes;    /* 存储配额（0 = 不限） */
  uint64_t used_bytes;     /* 已用空间 */
  time_t created_at;
  time_t last_login;
} user_t;

/* ========================================================================
 * 十一、常用缓冲区大小宏
 * ======================================================================== */

#define PATH_MAX_LEN 512     /* 路径最大长度 */
#define FILENAME_MAX_LEN 256 /* 文件名最大长度 */
#define SHA256_HEX_LEN 65 /* SHA256 十六进制字符串长度（含 \0） */
#define SHA256_RAW_LEN 32 /* SHA256 原始二进制长度 */
#define JWT_MAX_LEN 512   /* JWT 令牌最大长度 */
#define HOST_MAX_LEN 64   /* 主机名/IP 最大长度 */

#endif
