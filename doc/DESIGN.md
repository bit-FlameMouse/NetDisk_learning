# NetDisk — 技术设计文档 ★★★★★

> **版本**: v1.0 | **纯 C 语言实现** | **学习版**
>
> NDP-Lite 自定义二进制协议 | epoll 高并发 | MySQL 虚拟路径 | SHA256 秒传去重

---


## 1. 项目概述

### 1.1 项目定位

NetDisk 是一款基于**自定义二进制协议（NDP-Lite）**的类 CLI 网络磁盘服务端。系统采用纯 C 语言实现，通过 epoll 事件驱动 + 线程池实现高并发连接，基于 MySQL 邻接表模型构建虚拟目录树。

### 1.2 核心技术特征

| 特征 | 实现 | 亮点 |
|------|------|------|
| **NDP-Lite 二进制协议** | 8 字节定长帧头 + TLV 载荷编码 | 典型请求 22 字节 vs HTTP 200 字节 |
| **CLI 交互模型** | 类 Linux 命令行（`ls`、`cd`、`upload`、`download`） | 零学习成本，支持短命令别名 |
| **高并发架构** | epoll ET 边缘触发 + 工作线程池 + 时间轮超时管理 | 万级并发连接 |
| **虚拟路径** | MySQL 邻接表目录树 | 路径与物理存储完全解耦 |
| **内容寻址** | SHA256 去重 + PUT 内置秒传检测 | 重复文件零传输 |

> 📌 **学习版定位**：协议通信为明文传输，用户密码以 SHA256 + 随机盐哈希存储，使用 JWT 进行无状态认证。不包含传输加密（TLS/AES）。生产环境适配方案见 [§13 后续版本规划](#13-后续版本规划)。

### 1.3 适用场景

- 🎓 学习 Linux 网络编程、自定义协议设计、并发编程
- 🖥️ 运维团队远程文件管理与自动化脚本集成
- 🏠 内网环境下的轻量文件共享

---

## 2. 技术选型

> **选型原则**：纯 C + 最少依赖 + 自研核心模块。学习项目中能用标准库解决的不用第三方库。

| 层面 | 技术 | 理由 |
|------|------|------|
| 网络 I/O | **epoll (ET 模式)** | Linux 内核原生，单线程管理数万连接；ET 模式减少事件通知次数 |
| 并发模型 | **Reactor + 线程池** | epoll 处理 I/O（主线程），线程池处理阻塞操作（DB、文件）；职责分离 |
| 通信协议 | **NDP-Lite** | 自研 8 字节帧头 + 二进制 TLV 载荷，明文传输；比 HTTP 节省 ~90% 协议开销 |
| 认证 | **JWT (HS256)** | 无状态认证，内存验签，零数据库查询；15 分钟过期 |
| 密码存储 | **SHA256 + 盐** | 32 字节随机盐 + SHA256 哈希；学习用途的安全基线 |
| 数据序列化 | **二进制 TLV** | Type-Length-Value 紧凑编码，无字符串转义开销 |
| 数据库 | **MySQL 8.0** | libmysqlclient 直连；虚拟路径 + 事务支持 |
| 文件存储 | **本地 ext4/xfs** | 直接 I/O，SHA256 哈希目录分片（256 个子目录） |
| 超时管理 | **时间轮** | O(1) 添加/删除/到期，比链表遍历或最小堆更适合万级连接 |
| 构建 | **CMake + GCC** | 跨发行版兼容，两个 target（服务端 + 客户端） |


---

## 3. 系统架构分层

### 3.1 六层架构全景

```
┌──────────────────────────────────────────────────────────────────┐
│                        客户端层                                   │
│            CLI 客户端 (类 shell 交互) / 脚本客户端                 │
├──────────────────────────────────────────────────────────────────┤
│                      网络接入层                                    │
│     epoll 事件循环  │  帧解析器 (5 状态机)  │  连接管理 (Keep-Alive) │
├──────────────────────────────────────────────────────────────────┤
│                  命令路由 & 中间件层                               │
│     命令分发器 (O(1) 跳转)  │  JWT 认证  │  令牌桶限流 & 日志      │
├──────────────────────────────────────────────────────────────────┤
│                      业务逻辑层                                    │
│   虚拟文件系统 (VFS)  │  存储引擎 (上传/下载/去重/秒传/断点续传)    │
│                      [ 工作线程池 — CPU 核数 × 2 ]                  │
├──────────────────────────────────────────────────────────────────┤
│                      数据访问层                                    │
│       MySQL 连接池 (预连接 + 互斥锁 + 条件变量)  │  文件适配层      │
├──────────────────────────────────────────────────────────────────┤
│                       物理存储层                                   │
│           MySQL 8.0 数据库 (3 张表)  │  本地磁盘 / NFS             │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 各层职责

#### 网络接入层 (`src/net/`)

- **epoll 事件循环**：主线程运行，ET 模式，1 秒超时驱动时间轮
- **帧解析器**：5 状态状态机解析 NDP 帧头（8 字节）→ 读载荷 → 分发到线程池
- **连接管理**：Keep-Alive 长连接，空闲 60 秒超时踢出

#### 命令路由 & 中间件层 (`src/route/`)

- **命令分发器**：CMD 码 → handler 函数指针，O(1) 跳转；14 条目跳转表
- **JWT 认证**：内存验签 HMAC-SHA256，提取 `sub` 作为 user_id —— 零数据库查询
- **限流中间件**：令牌桶算法，按用户 ID 限流（PUT 秒传 30 次/分钟）

#### 业务逻辑层 (`src/biz/`)

- **虚拟文件系统**：MySQL 邻接表目录树，路径逐级解析；`cd`、`ls`、`mkdir`、`rm`、`mv`
- **存储引擎**：流式上传/下载、SHA256 去重、秒传检测、断点续传状态管理、CRC32 校验

#### 数据访问层 (`src/data/`)

- **MySQL 连接池**：预连接 + 互斥锁 + 条件变量；`db_acquire()` / `db_release()` 接口
- **文件存储适配层**：基于 SHA256 哈希的分目录物理存储（`data/XX/hash`）

#### 物理存储层

- **MySQL 8.0**：`users`、`nodes`、`upload_sessions` 三张表
- **本地磁盘**：`data/XX/hash` 路径存储文件实体，支持 NFS 挂载

### 3.3 一次请求的完整数据流

```
客户端: ls /docs/work/
  │
  ▼
[1] TCP 到达 → epoll 触发 EPOLLIN
[2] 帧解析器: 读 8 字节帧头 → 读 14 字节 TLV 载荷 → FRAME_DONE
[3] 投递到线程池: task{handler=handle_ls, conn, frame}
  │
  ▼  [Worker 线程]
[4] TLV 解码: PATH = "/docs/work/"
[5] ★ JWT 认证: jwt_verify(token, secret, &user_id) → user_id=42
[6] ★ LRU 缓存: cache_get("42:/docs/work/") → 未命中
[7] VFS 路径解析: 逐级 SQL → node_id=15
[8] ★ LRU 写入: cache_put("42:/docs/work/", 15)
[9] VFS 列出目录: SELECT ... FROM nodes WHERE parent_id=15
[10] TLV 编码响应: 每个条目一个 ENTRY_START/END 包裹
[11] conn_send_frame() → 写入 conn->send_buf
[12] write(eventfd, ...) → 通知主线程可写
  │
  ▼  [主线程]
[13] epoll 收到 eventfd → 注册 conn->fd 的 EPOLLOUT
[14] epoll 触发 EPOLLOUT → write(sockfd, send_buf) → 数据发出
[15] timer_refresh() → 重置连接空闲超时
```


### 3.4 协议选型分析

| 维度 | HTTP/REST 方案 | NDP-Lite（本方案） |
|------|---------------|-------------------|
| 帧格式 | 文本行 + multipart 编码 | 8 字节定长帧头 + 二进制 TLV 载荷 |
| 典型请求 | `POST /api/v1/files/upload` (~200B) | `CMD=0x15` + 14B TLV 载荷 (**总计 22B**) |
| 参数传递 | URL query / JSON body | 二进制 TLV 编码 |
| 传输方式 | TCP + TLS 层 | TCP 直连 (学习版明文) |
| 流式传输 | HTTP `chunked` 编码 | FLAGS.STREAMING 位 + 多帧连续传输 |
| 解析模型 | 逐字节文本状态机 | **定长帧头直接寻址，O(1) 字段跳转** |
| 带宽效率 | JSON 文本 + Base64 ≈ 33% 膨胀 | **二进制紧凑编码，零冗余** |
| 客户端依赖 | 需 HTTP 库（libcurl ~1MB） | 自研协议栈，约 280 行 C 代码 |

> 💡 **选型理由**：NDP-Lite 牺牲了 HTTP 的通用性和工具链，换取了带宽效率、解析速度和零外部依赖。对于一个内网文件传输工具，这个取舍是合理的。如果需要 HTTP 兼容，可以在上层加一个 HTTP-to-NDP 网关。

---

## 4. 核心模块设计

### 4.1 模块划分与源文件结构

```
netdisk/
├── CMakeLists.txt                        # 顶层构建 (服务端 + 客户端两个 target)
│
├── src/                                  # ═══ 服务端源码 ═══
│   ├── main.c                            #   入口: 参数解析、守护进程、信号处理、主循环
│   │
│   ├── net/  ── 网络接入层 (§3) ──
│   │   ├── server.c/h                    #   epoll ET 事件循环、连接 accept/close、eventfd 通知
│   │   └── timer.c/h                     #   时间轮: 60 槽位、O(k) 到期、空闲踢出
│   │
│   ├── proto/  ── 协议层 ──
│   │   ├── types.h                       #   全局类型: 帧头结构、CMD 码枚举、FLAGS 位掩码、TLV Type
│   │   ├── tlv.c/h                       #   TLV 编解码: Type-Length-Value 序列化，边界检查
│   │   └── protocol.c/h                  #   NDP 帧解析状态机 (5 状态) + 帧构造 + 收发缓冲
│   │
│   ├── route/  ── 命令路由 & 中间件层 (§3) ──
│   │   ├── dispatcher.c/h                #   CMD→handler 跳转表 (14 条目) + 令牌桶限流
│   │   ├── handler.c/h                   #   12 个命令处理器 (认证 3 + 文件 7 + 高级 2)
│   │   ├── auth.c/h                      #   用户认证: register 加盐哈希、login JWT 签发、logout
│   │   └── jwt.c/h                       #   JWT HS256 生成与验证、Base64URL 编解码
│   │
│   ├── biz/  ── 业务逻辑层 (§3) ──
│   │   ├── vfs.c/h                       #   虚拟文件系统: 路径逐级解析、mkdir/list/delete/move/create
│   │   └── storage.c/h                   #   存储引擎: 流式上传/下载、SHA256 去重、秒传、断点续传、CRC32
│   │
│   ├── data/  ── 数据访问层 (§3) ──
│   │   └── db.c/h                        #   MySQL 预连接池: 互斥锁+条件变量，db_acquire/db_release
│   │
│   └── base/  ── 基础设施 (跨层共用) ──
│       ├── config.c/h                    #   INI 格式配置解析，10 个配置段
│       ├── log.c/h                       #   异步日志: 环形缓冲 + 文件轮转 + 分级输出
│       ├── thread_pool.c/h               #   工作线程池: 环形任务队列 + 互斥锁+条件变量
│       └── utils.c/h                     #   工具集: SHA256、Base64、字符串处理、MIME 检测
│
├── client/                               # ═══ CLI 客户端源码 ═══
│   ├── main.c                            #   入口: 读取 ~/.netdisk.conf、TCP 连接、REPL 交互循环
│   ├── protocol.c/h                      #   客户端协议栈: 帧收发、阻塞读响应
│   └── commands.c/h                      #   12 个命令 + 进度条 + 本地 SHA256 计算
│
├── config/                               # ═══ 配置文件 ═══
│   ├── netdisk.conf                      #   服务端配置模板
│   └── client.conf                       #   客户端配置模板
│
├── sql/                                  # ═══ 数据库脚本 ═══
│   └── schema.sql                        #   3 张表 DDL: users、nodes、upload_sessions
│
└── tests/                                # ═══ 单元测试 ═══
    ├── test_tlv.c                        #   TLV 编解码正确性
    ├── test_vfs.c                        #   虚拟路径解析
    └── test_protocol.c                   #   帧解析状态机
```

**模块依赖关系**：

```
main.c
  └─ server.c ──────── timer.c
       │                  │
       ├─ protocol.c ── tlv.c ── types.h
       │
       ├─ dispatcher.c ── handler.c
       │      │               ├─ auth.c ── jwt.c
       │      │               ├─ vfs.c ──── db.c
       │      │               └─ storage.c ─ db.c
       │      │
       │      └─ [限流中间件: 令牌桶]
       │
       ├─ thread_pool.c
       ├─ log.c
       ├─ config.c
       └─ utils.c
```

> 📐 **依赖规则**：箭头方向 = `#include` 方向。底层模块不依赖上层。所有模块共享 `types.h`。

---

## 5. NDP-Lite 协议设计

### 5.1 帧格式

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      'N'      |      'D'      |    Version    |     CMD       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    FLAGS      |     SEQ       |        Payload Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                      Payload (明文 TLV)                       |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**帧头固定 8 字节**（明文传输）：

| 偏移 | 大小 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 2 | Magic | `uint8[2]` | `N` `D` (0x4E 0x44) — 帧同步标识，用于在 TCP 流中定位帧边界 |
| 2 | 1 | Version | `uint8` | 协议版本，当前 `0x01` |
| 3 | 1 | CMD | `uint8` | 命令码（见 [§5.2 命令码定义](#52-命令码定义)） |
| 4 | 1 | FLAGS | `uint8` | 标志位（见下方 FLAGS 位定义） |
| 5 | 1 | SEQ | `uint8` | 序列号，请求-响应匹配（0-255 循环） |
| 6 | 2 | Payload Length | `uint16` (BE) | 载荷长度，单帧上限 **64KB** |
| 8 | — | Payload | `uint8[]` | 明文 TLV 载荷 |

**FLAGS 位定义**：

| 位 | 掩码 | 常量名 | 含义 |
|----|------|--------|------|
| 0 | `0x01` | `STREAMING` | 流式传输 — 本帧后还有后续帧（大数据分片） |
| 1 | `0x02` | `IS_RESPONSE` | 响应帧 — 本帧是服务端响应（客户端请求帧此位为 0） |
| 2-7 | — | — | 保留，必须置 0 |

**FLAGS 组合使用示例**：

| FLAGS 值 | 含义 |
|----------|------|
| `0x00` | 客户端请求帧，单帧完成 |
| `0x01` | 客户端请求帧，流式传输中（后面还有） |
| `0x02` | 服务端响应帧，单帧完成 |
| `0x03` | 服务端响应帧，流式传输中（`STREAMING | IS_RESPONSE`） |

### 5.2 命令码定义

#### 认证命令 (CMD < 0x10)

| CMD | 名称 | 方向 | 说明 |
|-----|------|------|------|
| `0x02` | `AUTH` | C→S | 登录：携带用户名+密码 |
| `0x03` | `AUTH_OK` | S→C | 登录成功：返回 JWT token |
| `0x04` | `AUTH_ERR` | S→C | 认证失败：用户名或密码错误 |
| `0x07` | `BYE` | 双向 | 断开连接 |
| `0x08` | `REGISTER` | C→S | 注册新用户：用户名+密码 |

#### 文件操作命令

| CMD | 名称 | 方向 | 说明 |
|-----|------|------|------|
| `0x10` | `LS` | C→S | 列出目录内容 |
| `0x11` | `CD` | C→S | 切换工作目录（服务端仅验证路径存在） |
| `0x12` | `MKDIR` | C→S | 创建目录 |
| `0x13` | `RM` | C→S | 删除文件/目录（递归删除目录） |
| `0x14` | `MV` | C→S | 移动/重命名 |
| `0x15` | `PUT` | C→S | 上传文件（内置秒传检测） |
| `0x16` | `GET` | C→S | 下载文件 |
| `0x17` | `STAT` | C→S | 获取文件/目录元信息 |

#### 高级操作命令

| CMD | 名称 | 方向 | 说明 |
|-----|------|------|------|
| `0x20` | `RESUME_PUT` | C→S | 断点续传上传 |
| `0x21` | `RESUME_GET` | C→S | 断点续传下载 |



#### 响应码（放在 CMD 字段中返回）

| CMD | 名称 | 说明 |
|-----|------|------|
| `0x80` | `OK` | 操作成功 |
| `0x81` | `ERR` | 通用错误 |
| `0x82` | `ERR_AUTH` | 认证失败（token 无效或过期） |
| `0x83` | `ERR_EXISTS` | 文件/目录已存在 |
| `0x84` | `ERR_NOT_FOUND` | 文件/目录不存在 |
| `0x85` | `ERR_QUOTA` | 配额超限 |
| `0x86` | `ERR_ACCESS` | 权限不足 |
| `0x87` | _(保留)_ | 预留扩展 |
| `0x88` | `ERR_RANGE` | 断点续传偏移无效 |

> 📐 **错误码编码约定**：细分错误码（`0x81`-`0x88`）直接放在响应帧的 **CMD 字段**中，提供 O(1) 的错误类型判断，无需解析 TLV 载荷。TLV 中的 `ERROR_MSG`（Type=`0x09`）仅携带人类可读的错误描述（如 `"Directory not empty"`），用于客户端日志和用户提示。

**当前版本共 22 个有效命令码 + 1 个保留位。**以下功能纳入后续版本规划：`SEARCH`（全文搜索）、`ZIPGET`（打包下载）、`SHARE`（分享链接）。详见 [§13 后续版本规划](#13-后续版本规划)。

### 5.3 TLV 载荷编码

```
Type (1 byte) | Length (2 bytes, big-endian) | Value (Length bytes)
```

#### 完整 TLV Type 表

| Type | 常量名 | Value 类型 | 说明 |
|------|--------|-----------|------|
| `0x01` | `PATH` | 字符串 | 文件/目录路径 |
| `0x02` | `FILENAME` | 字符串 | 文件名 |
| `0x03` | `FILE_SIZE` | `uint64` (8B) | 文件大小 |
| `0x04` | `FILE_HASH` | `uint8[32]` | SHA256 哈希值 |
| `0x05` | `MIME_TYPE` | 字符串 | MIME 类型 |
| `0x06` | `TOKEN` | 字符串 | JWT token |
| `0x07` | `USERNAME` | 字符串 | 用户名 |
| `0x08` | `PASSWORD` | 字符串 | 密码（明文传输，学习版） |
| `0x09` | `ERROR_MSG` | 字符串 | 错误描述 |
| `0x0A` | `FILE_ID` | `uint64` (8B) | 文件 node_id |
| `0x0B` | `PARENT_ID` | `uint64` (8B) | 父目录 node_id |
| `0x0C` | `OFFSET` | `uint64` (8B) | 断点续传 / 多点下载偏移 |
| `0x0D` | `CHUNK_SEQ` | `uint32` (4B) | 分片序号 |
| `0x0E` | `CHUNK_DATA` | 二进制 | 文件分片数据 |
| `0x0F` | `CRC32` | `uint32` (4B) | CRC32 校验值 |
| `0x10` | `ACTION` | `uint8` (1B) | 命令子动作 |
| `0x11` | `UPLOAD_ID` | `uint64` (8B) | 上传会话 ID |
| `0x12` | `MSG` | 字符串 | 通用消息 |
| `0x13` | `ENTRY_TYPE` | `uint8` (1B) | 条目类型: `0x01`=目录, `0x02`=文件 |
| `0xFE` | `ENTRY_START` | (Length=0) | LS 响应条目起始标记 |
| `0xFF` | `ENTRY_END` | (Length=0) | LS 响应条目结束标记 |

> 📐 **条目边界说明**：`ENTRY_START` / `ENTRY_END` 用于显式标记列表中每个条目的边界。LS 响应中的每个目录项被这对标记包裹，中间可包含多个 TLV 字段。整个列表的终结使用 `ENTRY_END` + `Length=1, Value=0x00`。

#### 编码示例 — LS 请求

```
命令帧: CMD=0x10 (LS), SEQ=1, FLAGS=0x00
Payload Length = 14 字节:

  [0x01] [0x00 0x0B] "/docs/work"
   Type   Length=11   Value (11 bytes)

→ 帧总长 = 8 (帧头) + 14 (载荷) = 22 字节

对比: GET /api/v1/files?path=%2Fdocs%2Fwork + HTTP 头部 ≈ 200 字节
→ NDP-Lite 节省约 89% 协议开销
```

#### 编码示例 — LS 响应

```
响应帧: CMD=0x80 (OK), SEQ=1, FLAGS=0x02 (IS_RESPONSE)
TLV 载荷:

  [0x01] [0x00 0x0B] "/docs/work"         ← 路径

  [0xFE] [0x00 0x00]                      ← 条目1 开始
    [0x02] [0x00 0x07] "archive"          ←   目录名
    [0x13] [0x00 0x01] 0x01               ←   类型: directory
    [0x03] [0x00 0x00]                    ←   目录无 size
  [0xFF] [0x00 0x00]                      ← 条目1 结束

  [0xFE] [0x00 0x00]                      ← 条目2 开始
    [0x02] [0x00 0x09] "report.pdf"       ←   文件名
    [0x13] [0x00 0x01] 0x02               ←   类型: file
    [0x03] [0x00 0x08] 2048576            ←   大小: 2.0 MB
    [0x05] [0x00 0x0F] "application/pdf"  ←   MIME 类型
  [0xFF] [0x00 0x00]                      ← 条目2 结束

  [0xFF] [0x00 0x01] 0x00                 ← 列表结束 (ENTRY_END + 终止标记)
```

> 💡 **扩展性设计**：当条目将来需要扩展字段（如权限掩码、创建时间）时，解析器只需在 `ENTRY_START` 和 `ENTRY_END` 之间追加新 TLV 即可，不会破坏现有解析逻辑。

### 5.4 帧解析状态机

```c
typedef enum {
    FRAME_MAGIC1,     // 状态 0: 等待 'N' (0x4E)
    FRAME_MAGIC2,     // 状态 1: 等待 'D' (0x44)
    FRAME_HEADER,     // 状态 2: 读剩余 6 字节帧头 (version+cmd+flags+seq+len)
    FRAME_PAYLOAD,    // 状态 3: 读 payload_len 字节载荷
    FRAME_DONE        // 状态 4: 帧完整，可投递处理
} frame_state_t;
```

**状态转换图**：

```
  ┌──────────┐   byte='N'   ┌──────────┐   byte='D'   ┌──────────┐
  │  MAGIC1  │─────────────►│  MAGIC2  │─────────────►│  HEADER  │
  │  等 'N'  │◄─────────────│  等 'D'  │◄─────────────│ 读 6 字节 │
  └──────────┘  byte≠'N'   └──────────┘  byte≠'D'   └────┬─────┘
       ▲                                                  │
       │               ┌──────────┐   读完 payload_len    │
       │   读到        │  DONE    │◄──────────────────────│
       └───────────────│  投递    │                       │
           下一帧      └──────────┘   ┌──────────┐        │
                                      │ PAYLOAD  │◄───────┘
                                      │ 读载荷   │
                                      └──────────┘
```

**关键特性**：
- 2 字节 Magic Number（`N` `D`）提供帧同步——在 TCP 字节流中可靠定位帧边界
- 状态机在每个 `epoll` 读事件中推进，不阻塞
- 每连接独立状态——不同连接的解析进度互不影响
- 非法字节触发状态回退到 `MAGIC1`，自动重新同步

### 5.5 流式传输 (STREAMING 标志)

大文件上传/下载使用多帧流式传输。核心机制：`FLAGS.STREAMING` 位置 1 表示"后面还有数据帧"，置 0 表示传输结束。

#### PUT 命令两阶段交互

```
阶段一: 元信息 + 秒传检测（单帧）
─────────────────────────────────────────
  客户端 → PUT 首帧 (FLAGS=0x00)
    TLV: [PATH] [FILENAME] [FILE_SIZE] [FILE_HASH]

  服务端:
    → SELECT content_hash FROM nodes WHERE content_hash=? → 秒传检测
    → hash 命中: 返回 OK (FLAGS=0x02) + TLV [FILE_ID] [MSG="INSTANT saved"]
    → hash 未命中: 返回 OK (FLAGS=0x03) + TLV [ACTION="SEND_DATA"]

阶段二: 流式数据传输（仅 hash 未命中时进入）
─────────────────────────────────────────
  客户端 → PUT 数据帧 (FLAGS=0x01, STREAMING)
    TLV: [CHUNK_DATA]
  客户端 → PUT 数据帧 (FLAGS=0x01)
    TLV: [CHUNK_DATA]
  ...
  客户端 → PUT 终结帧 (FLAGS=0x00, STREAMING 清除)
    TLV: [CHUNK_DATA] [CRC32]

  服务端:
    → 校验 CRC32 → SHA256 流式计算 → rename 临时文件
    → 返回 OK (FLAGS=0x02) + TLV [FILE_ID] [FILE_HASH]
```

#### GET 命令流式响应

```
客户端 → GET (FLAGS=0x00), TLV: [PATH]

服务端:
  → 查 nodes → 权限检查
  → 返回 OK (FLAGS=0x03), TLV: [FILE_SIZE] [FILE_HASH] [MIME_TYPE]
  → 开始流式发送:
      DATA 帧 (FLAGS=0x03), TLV: [CHUNK_DATA]  ← sendfile 零拷贝
      DATA 帧 (FLAGS=0x03), TLV: [CHUNK_DATA]
      ...
      END 帧  (FLAGS=0x02), TLV: [CHUNK_DATA] [CRC32]
```

---

## 6. 认证设计

### 6.1 用户注册（加盐哈希）

```
客户端                                    服务端
  │  REGISTER (CMD=0x08)                   │
  │  TLV: [USERNAME="alice"]              │
  │       [PASSWORD="mypassword"]         │
  │──────────────────────────────────────►│
  │                                        │ ① 检查用户名唯一性
  │                                        │ ② 生成 32 字节随机 salt
  │                                        │ ③ password_hash = SHA256(password + salt)
  │                                        │ ④ INSERT INTO users (...)
  │  OK (CMD=0x80)                        │
  │  TLV: [USER_ID=42] [MSG="Registered"] │
  │◄──────────────────────────────────────│
  │                                        │
  │ 客户端自动发起 login（注册后自动登录）    │
```

### 6.2 JWT 登录

```
客户端                                    服务端
  │  AUTH (CMD=0x02)                       │
  │  TLV: [USERNAME] [PASSWORD]           │
  │──────────────────────────────────────►│
  │                                        │ ① SELECT salt, password_hash FROM users
  │                                        │ ② SHA256(password + salt) == stored_hash?
  │                                        │    → 匹配: jwt_generate(user_id, secret)
  │                                        │      返回 AUTH_OK + TLV [TOKEN]
  │                                        │    → 不匹配: 返回 AUTH_ERR
  │  AUTH_OK (CMD=0x03)                    │
  │  TLV: [TOKEN="eyJhbGciOiJ..."]        │
  │◄──────────────────────────────────────│
```

后续请求携带 JWT，服务端 `jwt_verify()` 在内存中验签——提取 `sub` 作为 user_id，**无需查数据库**。


### 6.3 注册后自动登录

```c
int cmd_register(int sockfd, const char *user, const char *pass) {
    cli_send_cmd(sockfd, CMD_REGISTER, TLV [USERNAME, PASSWORD]);
    frame_t resp; cli_recv_frame(sockfd, &resp);
    if (resp.hdr.cmd != CMD_OK) return -1;

    printf("[OK] Registered. Auto-logging in...\n");
    return cmd_login(sockfd, user, pass);  // 自动登录，无需用户再输密码
}
```

用户全程只输一次密码。注册成功即登录。

### 6.4 多点并行下载

大文件下载时客户端可多线程分块下载。每个线程独立 TCP 连接，通过 `OFFSET` TLV 字段指定起始偏移：

```
客户端 (4 线程并行下载 100MB 文件):
  Thread 1: GET [PATH="/file.iso"] [OFFSET=0]           → 0-25MB
  Thread 2: GET [PATH="/file.iso"] [OFFSET=26214400]    → 25-50MB
  Thread 3: GET [PATH="/file.iso"] [OFFSET=52428800]    → 50-75MB
  Thread 4: GET [PATH="/file.iso"] [OFFSET=78643200]    → 75-100MB

服务端: lseek(fd, offset) + sendfile 并发响应
客户端: 合并分块写入本地文件
```

---

## 7. CLI 命令集

### 7.1 客户端交互模型

客户端启动时自动读取 `~/.netdisk.conf` 获取服务端地址，**零参数启动**。

```
$ netdisk                              ← 零参数，自动读取配置连接
Connecting to 192.168.1.100:8443...   [OK]

NetDisk> register alice               ← 注册后自动登录
Password: ********
Confirm:  ********
[OK] Registered. Auto-logging in...
[OK] Logged in as alice (quota: 10.00 GB, used: 0 B)

NetDisk> ls /
  [DIR]  docs/
  [DIR]  videos/

NetDisk> login bob                    ← 已有账户直接登录
Password: ********
[OK] Logged in as bob (quota: 10.00 GB, used: 2.34 GB)

NetDisk> cd /docs/work/
NetDisk> pwd                          ← 纯本地操作，不发网络请求
  /docs/work

NetDisk> ll /docs/work/               ← ls 详情模式
  TYPE   NAME           SIZE      MODIFIED            MIME
  DIR    archive/       —         2026-05-20 10:00    —
  FILE   report.pdf     2.0 MB    2026-05-25 14:30    application/pdf
  FILE   draft.docx     156 KB    2026-05-28 09:15    application/vnd...

NetDisk> put report_v2.pdf /docs/work/
Computing SHA256...  a1b2c3d4e5f6...
[INSTANT] File already exists, skipped upload.  (2.0 MB saved)
[OK] File created: id=43

NetDisk> put new_file.pdf /docs/work/
Uploading: [████████████████████] 100% (1.5 MB)  Done.
[OK] File created: id=44, hash=f6e5d4c3b2a1...

NetDisk> get /docs/work/report.pdf ./downloads/
Downloading: [████████████████████] 100% (2.0 MB)  Done.

NetDisk> mv /docs/work/draft.docx /docs/work/final.docx
[OK] Moved.

NetDisk> stat /docs/work/report.pdf
  Name:  report.pdf    Size: 2.0 MB    Type: application/pdf
  Hash:  a1b2c3d4...   Created: 2026-05-25 14:30

NetDisk> logout
[OK] Bye.
```

### 7.2 完整命令列表

#### 连接与会话

| 命令 | NDP CMD | 说明 |
|------|---------|------|
| `register <user>` | `REGISTER` (0x08) | 注册新用户，成功后自动登录 |
| `login <user>` | `AUTH` (0x02) | 登录已有账户 |
| `logout` | `BYE` (0x07) | 断开连接 |
| `whoami` | — | 本地：显示当前用户 |

#### 目录与文件浏览

| 命令 | 别名 | NDP CMD | 说明 |
|------|------|---------|------|
| `ls [path]` | `dir` | `LS` (0x10) | 列出目录内容，默认当前路径 |
| `ll [path]` | — | `LS` (0x10) | `ls -l` 等价，显示大小/日期 |
| `cd <path>` | — | `CD` (0x11) | 切换工作目录（服务端仅验证） |
| `pwd` | — | 本地 | 显示当前路径，不发网络请求 |
| `stat <path>` | — | `STAT` (0x17) | 显示单个文件完整元信息 |

#### 文件操作

| 命令 | 别名 | NDP CMD | 说明 |
|------|------|---------|------|
| `mkdir <path>` | — | `MKDIR` (0x12) | 创建目录 |
| `rm <path>` | `del` | `RM` (0x13) | 删除文件/目录（递归删目录） |
| `mv <src> <dst>` | `rename` | `MV` (0x14) | 移动或重命名 |
| `upload <local> <remote>` | `put` | `PUT` (0x15) | 上传文件（自动秒传检测） |
| `download <remote> [local]` | `get` | `GET` (0x16) | 下载文件 |

> 📐 **长短命令**：短命令（`ls`/`put`/`get`/`rm`）和长命令（`upload`/`download`/`delete`）等价。客户端解析层做别名映射，协议层只用 CMD 码。`ll` 是 `ls` 的详情模式——同一 LS 帧，服务端多带 `FILE_SIZE`、`MIME_TYPE`、`updated_at`。

#### 高级操作

| 命令 | NDP CMD | 说明 |
|------|---------|------|
| `resume-put <local> <remote>` | `RESUME_PUT` (0x20) | 断点续传上传 |
| `resume-get <remote>` | `RESUME_GET` (0x21) | 断点续传下载 |

### 7.3 `cd` 与 `pwd` 的路径管理

`cd` 和 `pwd` 是**客户端本地**的路径状态管理，不操作服务端文件系统：

```c
static char g_cwd[512] = "/";  // 客户端维护当前工作路径

int cmd_cd(int sockfd, const char *path) {
    char new_path[512];

    if (path[0] == '/') {
        strcpy(new_path, path);           // 绝对路径直接用
    } else if (strcmp(path, "..") == 0) {
        parent_dir(g_cwd, new_path);      // 向上一级
    } else {
        snprintf(new_path, sizeof(new_path), "%s/%s", g_cwd, path);
    }
    normalize_path(new_path);  // 去掉多余的 // 和 /./

    // 发送 CD 帧，验证路径是否存在
    cli_send_cmd(sockfd, CMD_CD, TLV [PATH=new_path]);
    frame_t resp; cli_recv_frame(sockfd, &resp);

    if (resp.hdr.cmd == CMD_OK) {
        strcpy(g_cwd, new_path);  // 验证通过才更新客户端路径
    }
    return (resp.hdr.cmd == CMD_OK) ? 0 : -1;
}

// pwd 纯本地操作，不发网络请求
int cmd_pwd(void) { printf("%s\n", g_cwd); return 0; }
```

服务端 `handle_cd` 仅验证路径存在——调用 `vfs_resolve_path()` 成功则返回 OK。**不修改任何服务端状态**。

### 7.4 上传 (PUT) 完整交互

```
客户端                                         服务端
  │                                               │
  │ 客户端先计算文件 SHA256 (本地, 1GB 约 0.3 秒)   │
  │                                               │
  │ PUT (CMD=0x15, SEQ=1, FLAGS=0x00)             │
  │ TLV: [PATH] [FILENAME] [FILE_SIZE]             │
  │      [FILE_HASH="a1b2c3d4..."]                 │
  │──────────────────────────────────────────────►│
  │                                               │ ① 限流检查 (30次/分钟/用户)
  │                                               │ ② 秒传检测:
  │                                               │    SELECT id FROM nodes
  │                                               │    WHERE content_hash=? AND is_deleted=0
  │                                               │
  │  ╔═══════ hash 命中 → 秒传! ═══════╗          │
  │  ║ OK (0x80, FLAGS=0x02)          ║          │
  │  ║ TLV: [FILE_ID=43] [MSG]        ║          │
  │  ║◄───────────────────────────────║          │
  │  ║ → 直接 INSERT nodes, 跳过传输  ║          │
  │  ╚════════════════════════════════╝          │
  │                                               │
  │  ╔═══════ hash 未命中 → 正常传输 ═══════╗      │
  │  ║ OK (0x80, FLAGS=0x03)              ║      │
  │  ║ TLV: [ACTION="SEND_DATA"]          ║      │
  │  ║◄───────────────────────────────────║      │
  │  ║                                    ║      │
  │  ║ PUT_DATA (SEQ=2, FLAGS=0x01)       ║      │
  │  ║ TLV: [CHUNK_DATA] (4096 B)         ║ write(fd)
  │  ║───────────────────────────────────►║      │
  │  ║ ... 继续发送数据帧 ...              ║      │
  │  ║                                    ║      │
  │  ║ PUT_END (SEQ=N, FLAGS=0x00)        ║      │
  │  ║ TLV: [CHUNK_DATA] [CRC32]          ║ 校验 │
  │  ║───────────────────────────────────►║ SHA256│
  │  ║                                    ║ rename│
  │  ║   OK (0x80, FLAGS=0x02)            ║      │
  │  ║   TLV: [FILE_ID=44] [FILE_HASH]    ║      │
  │  ║◄───────────────────────────────────║      │
  │  ╚════════════════════════════════════╝      │
```

**秒传触发条件**：客户端在 PUT 首帧必须携带 `FILE_HASH`。服务端收到后查询 `idx_hash` 索引。命中则直接返回 `[FILE_ID]` + 秒传提示。**整个流程对用户透明**——用户只执行 `put file.pdf /docs/`，服务端自动决定走秒传还是正常传输。

**安全措施**：秒传检测复用 PUT 命令的限流（30 次/分钟/用户），防止 hash 遍历探测。

---

## 8. 核心模块详解

### 8.1 虚拟文件系统 (vfs.c)

基于 MySQL 邻接表模型实现虚拟目录树。路径与物理存储完全解耦——用户看到的目录结构存储在数据库中，物理文件以 SHA256 哈希值分散存储。

#### 路径解析算法

```
输入: "/docs/work/report.pdf" + user_id=42
输出: node_id=20

步骤:
1. 取用户根节点: SELECT id FROM nodes
                 WHERE user_id=42 AND parent_id IS NULL → id=5
2. 按 '/' 分割 → ["docs", "work", "report.pdf"]
3. 逐级查询:
   "docs"        → SELECT id FROM nodes WHERE parent_id=5  AND name='docs'        → 10
   "work"        → SELECT id FROM nodes WHERE parent_id=10 AND name='work'        → 15
   "report.pdf"  → SELECT id FROM nodes WHERE parent_id=15 AND name='report.pdf'  → 20
4. 任意一级查不到 → 返回 ERR_NOT_FOUND (0x84)
5. 返回 node_id = 20
```


#### 核心接口

```c
// 路径 → 节点 ID (逐级 SQL + LRU 缓存)
int vfs_resolve_path(uint64_t user_id, const char *path, uint64_t *node_id);

// 创建目录
int vfs_mkdir(uint64_t user_id, uint64_t parent_id, const char *name);

// 列出目录: SELECT ... WHERE parent_id=? ORDER BY node_type, name
int vfs_list_dir(uint64_t user_id, uint64_t dir_id, vfs_node_t **entries, int *count);

// 创建文件记录 (上传完成后调用，事务内执行)
int vfs_create_file(uint64_t user_id, uint64_t parent_id, const char *name,
                    uint64_t size, const char *hash, const char *mime);

// 软删除: UPDATE nodes SET is_deleted=1 WHERE id=?
int vfs_delete(uint64_t user_id, uint64_t node_id);

// 移动/重命名: UPDATE nodes SET parent_id=?, name=? WHERE id=?
int vfs_move(uint64_t user_id, uint64_t node_id, uint64_t new_parent, const char *new_name);
```

#### 关键 SQL

```sql
-- 列出目录内容 (最常见操作, 按类型+名称排序)
SELECT id, name, node_type, file_size, mime_type, updated_at
FROM nodes WHERE parent_id=? AND user_id=? AND is_deleted=0
ORDER BY node_type ASC, name ASC;

-- 同名冲突检测 (parent_id 可为 NULL)
SELECT COUNT(*) FROM nodes
WHERE parent_id<=>? AND name=? AND user_id=? AND is_deleted=0;

-- 用户空间统计
SELECT COALESCE(SUM(file_size), 0) FROM nodes
WHERE user_id=? AND node_type='file' AND is_deleted=0;
```

### 8.2 存储引擎 (storage.c)

负责文件物理存储、流式上传/下载、SHA256 去重、秒传检测和断点续传状态管理。

#### 物理存储布局

```
data/
├── 00/  00a1b2c3...fullsha256hash
├── 01/  01d4e5f6...
├── ...
├── fe/
└── ff/  ff3e8a9b...
tmp/     ← 上传中临时文件: tmp/{upload_id}/chunk_{seq}
```

> 📐 取 SHA256 前 2 字符分 256 个子目录，确保文件均匀分布，避免单目录 inode 耗尽。相同哈希文件物理只存一份——多个 nodes 记录可指向同一物理文件。

#### 上传流程（含秒传）

```
1. PUT 首帧到达 → 秒传检测:
   SELECT id FROM nodes WHERE content_hash=? AND is_deleted=0 LIMIT 1
   → 命中: 直接 INSERT nodes, 跳过以下步骤, 返回 FILE_ID + MSG="INSTANT saved"
   → 未命中: 继续

2. 流式接收数据帧 (STREAMING 模式):
   for each frame:
       write(tmp/{upload_id}/chunk_{seq})   ← 直接写磁盘
       UPDATE upload_sessions SET chunk_bitmap[seq]=1, completed_count++

3. 最后一帧到达 (STREAMING 位清除):
   verify CRC32(chunk_data)
   merge: 流式合并所有分片 → SHA256 流式计算 → 得到最终 hash
   dedup: SELECT id FROM nodes WHERE content_hash=? (二次确认)
   rename(tmp/{upload_id}/, data/XX/hash)   ← 原子移动
   事务: INSERT nodes + UPDATE users.used_bytes
   clean: DELETE tmp/{upload_id}/
```

#### 下载流程（sendfile 零拷贝）

```
1. GET 请求 → SELECT hash, size, mime FROM nodes WHERE id=?
2. 权限检查: user_id 匹配
3. 解析 OFFSET (可选): RESUME_GET → offset = 客户端本地文件大小
4. fd = open(data/XX/hash, O_RDONLY)
5. lseek(fd, offset, SEEK_SET)
6. sendfile(sockfd, fd, NULL, remaining_size)  ← 零拷贝, CPU ≈ 0
```

`sendfile()` 直接将数据从内核 page cache 传输到 socket buffer，全程不经过用户态缓冲区。


#### 秒传检测

```c
int storage_check_hash(const char *hash, uint64_t *existing_id) {
    // SELECT id FROM nodes WHERE content_hash=? AND is_deleted=0 LIMIT 1
    // 返回 >0 表示命中, *existing_id 为已有文件的 node_id
    // 调用方直接 vfs_create_file() 复用该 hash
}
```

命中时完全跳过文件传输。限流保护：同一用户 30 次/分钟。

#### 断点续传状态管理

```c
typedef struct {
    uint64_t upload_id;
    uint32_t chunk_size;      // 4MB
    uint32_t total_chunks;
    uint8_t  bitmap[512];     // bit[i]=1 表示第 i 片已完成
    uint32_t completed_count;
    int      status;          // CREATED → UPLOADING → MERGING → DONE → CANCELLED
} upload_session_t;
```

状态机：`CREATED → UPLOADING → (网络中断→查进度→UPLOADING) → MERGING → DONE`

### 8.3 MySQL 连接池 (db.c)

```c
typedef struct {
    MYSQL           **conns;      // 预分配的连接数组
    int              *in_use;     // 0=空闲 1=占用
    int               size;       // CPU核数 × 2 + 1
    pthread_mutex_t   lock;
    pthread_cond_t    cond;       // 无可用连接时 Worker 阻塞等待
} db_pool_t;

MYSQL *db_acquire(db_pool_t *pool);   // 获取连接, 无可用时阻塞
void   db_release(db_pool_t *pool, MYSQL *conn);  // 归还 + 唤醒等待者
int    db_execute(MYSQL *conn, const char *sql);  // INSERT/UPDATE/DELETE
int    db_query(MYSQL *conn, const char *sql, MYSQL_RES **result); // SELECT
```

**关键设计**：
- 启动时预创建全部连接，避免运行时建连开销
- 条件变量而非忙轮询：无连接时 Worker 进入内核休眠
- `mysql_ping()` 自动检测断连并重连，对上层透明
- 所有 SQL 使用参数化查询（`mysql_real_escape_string()`），防注入

### 8.4 时间轮 (timer.c)

管理所有连接的空闲超时——60 秒无操作自动踢出。

```c
#define TW_SLOTS    60       // 60 槽 = 60 秒
#define TW_INTERVAL 1        // 每秒走一格

typedef struct tw_task {
    int   fd;
    int   rotation;          // 剩余圈数 (超过一轮时使用)
    void (*callback)(int);   // 超时回调
    struct tw_task *next;    // 槽位链表
} tw_task_t;

typedef struct {
    tw_task_t *slots[TW_SLOTS];  // 60 个链表头
    int        current;           // 当前指针 0~59
    pthread_spinlock_t lock;      // 自旋锁 (<100ns 临界区)
} time_wheel_t;
```

**核心算法**：
```
timer_tick():                          // 每秒调用
  current = (current + 1) % 60
  for each task in slots[current]:
    if task.rotation == 0: callback(task.fd)
    else: task.rotation--

timer_add(fd, 60):
  slot = (current + 60) % 60
  rotation = (current + 60) / 60       // 超过 1 轮用 rotation 计数
  insert to slots[slot]

timer_refresh(fd):                     // 有活动时重置
  remove → timer_add(fd, 60)
```

**为什么选择时间轮**：

| 方案 | 添加 | 删除 | 到期 | 万级连接 |
|------|------|------|------|----------|
| 链表遍历 | O(1) | O(n) | O(n) | ❌ 不可用 |
| 最小堆 | O(log n) | O(log n) | O(log n) | ⚠️ 有波动 |
| **时间轮** | **O(1)** | **O(1)** | **O(k) ≈ O(1)** | ✅ 稳定 |

> 到期为 O(k)，k = 该槽位链表长度。万级连接均匀分布时每槽约 170 个任务，单次 `timer_tick()` 遍历在微秒级。

主线程在 `epoll_wait(1000ms)` 超时后调用 `timer_tick()`，每次仅检查 1 个槽位。选择自旋锁而非互斥锁的原因是临界区仅操作链表指针，执行时间 <100ns。

### 8.5 JWT 认证 (jwt.c)


```c
// 签发 (15 分钟有效)
char *jwt_generate(int user_id, const char *secret);

// 验证 (内存操作，零数据库查询)
// 返回: 0=通过, -1=签名无效, -2=已过期
int   jwt_verify(const char *token, const char *secret, int *user_id_out);
```

**Token 结构**：`Header.Payload.Signature`

```
Header:  {"alg":"HS256","typ":"JWT"}
Payload: {"sub":42,"iat":1717080000,"exp":1717080900}
Signature: HMAC-SHA256(base64url(Header) + "." + base64url(Payload), jwt_secret)
```

**安全要点**（学习版）：
- `jwt_secret` 存储在服务端配置文件，不在网络上传输
- 客户端不知道 `secret`，无法伪造签名
- 篡改 payload → 签名不匹配 → 返回 -1
- Token 过期 → `time() > exp` → 返回 -2
- 15 分钟过期：太短频繁重登，太长泄露窗口太大

### 8.6 线程池 (thread_pool.c)

```c
typedef void (*task_func_t)(void *arg);

typedef struct {
    task_func_t func;
    void       *arg;
} task_t;

typedef struct {
    task_t           *queue;          // 环形任务队列
    int               head, tail;
    int               size;           // 默认 4096
    int               count;
    pthread_mutex_t   lock;
    pthread_cond_t    not_empty;      // Worker 等待任务
    pthread_cond_t    not_full;       // 主线程等待队列空位
    pthread_t        *workers;
    int               num_workers;    // CPU 核数 × 2
} thread_pool_t;
```

**工作流程**：

```
主线程: 帧解析完成 → 封装 task{handler, conn, frame} → tp_enqueue()
Worker:  while(1) { task = dequeue(); task.func(task.arg); }

handler 内:
  1. TLV 解码请求参数
  2. JWT 验证 (jwt_verify)
  3. 调用 vfs / storage 业务模块
  4. TLV 编码响应
  5. conn_send_frame() → 写入 conn->send_buf (环形缓冲)
  6. write(eventfd, 1) → 通知主线程此连接可写
  7. timer_refresh() → 重置空闲超时
```

**send_buf 并发模型**：`conn->send_buf` 为环形缓冲区。写指针由 Worker 线程独占推进，读指针由主线程在 eventfd 通知后推进。读写之间通过内存屏障（`__sync_synchronize()`）保证可见性，不引入互斥锁。

---

## 9. 数据库设计

系统使用 3 张核心表，MySQL 8.0 InnoDB 引擎。

### 9.1 ER 关系

```
users (1) ────< (N) nodes (N) >──── (1) nodes (parent_id 自引用)
  │                  │
  │                  │ (1)
  │                  │
  └───< upload_sessions (N)
```

### 9.2 DDL

```sql
-- 用户表
CREATE TABLE users (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username    VARCHAR(64)   NOT NULL,
    password    CHAR(64)      NOT NULL COMMENT 'SHA256(password + salt)',
    salt        CHAR(32)      NOT NULL COMMENT '32字节随机盐，每用户独立',
    email       VARCHAR(128)  DEFAULT '',
    quota_bytes BIGINT UNSIGNED DEFAULT 10737418240 COMMENT '默认 10GB',
    used_bytes  BIGINT UNSIGNED DEFAULT 0,
    created_at  DATETIME      DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 虚拟路径节点表（核心表）
CREATE TABLE nodes (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id      BIGINT UNSIGNED NOT NULL,
    parent_id    BIGINT UNSIGNED DEFAULT NULL COMMENT 'NULL=根节点',
    name         VARCHAR(255)    NOT NULL,
    node_type    ENUM('file','directory') NOT NULL,
    file_size    BIGINT UNSIGNED DEFAULT 0,
    content_hash CHAR(64)        DEFAULT NULL COMMENT 'SHA256 hex, 仅 file 类型有值',
    mime_type    VARCHAR(128)    DEFAULT 'application/octet-stream',
    is_deleted   TINYINT(1)      DEFAULT 0 COMMENT '软删除标记',
    created_at   DATETIME        DEFAULT CURRENT_TIMESTAMP,
    updated_at   DATETIME        DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (parent_id) REFERENCES nodes(id),
    UNIQUE KEY uk_user_path (user_id, parent_id, name, is_deleted),
    INDEX idx_parent (parent_id),
    INDEX idx_hash (content_hash)   COMMENT '秒传检测 + 去重查询'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 断点上传会话表
CREATE TABLE upload_sessions (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id         BIGINT UNSIGNED NOT NULL,
    parent_id       BIGINT UNSIGNED NOT NULL,
    filename        VARCHAR(256)    NOT NULL,
    total_size      BIGINT UNSIGNED NOT NULL,
    chunk_size      INT UNSIGNED    DEFAULT 4194304 COMMENT '默认 4MB',
    total_chunks    INT UNSIGNED    NOT NULL,
    chunk_bitmap    VARBINARY(4096) NOT NULL COMMENT '位图: bit[i]=1 表示第i片已完成; 上限 total_chunks ≤ 32768',
    completed_count INT UNSIGNED    DEFAULT 0,
    status          ENUM('CREATED','UPLOADING','MERGING','DONE','CANCELLED') DEFAULT 'CREATED',
    created_at      DATETIME        DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME        DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id)   REFERENCES users(id),
    FOREIGN KEY (parent_id) REFERENCES nodes(id),
    INDEX idx_user_status (user_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

> 📐 `sessions` 表已移除——JWT 为无状态认证，token 在内存中验签即可，无需持久化。

---

## 10. 并发模型

### 10.1 Reactor + 线程池

```
┌─────────────────────────────────────────────────────────┐
│                    主线程 (Reactor)                       │
│                                                         │
│   epoll_wait(1000ms)                                    │
│     ├─ EPOLLIN  → 读帧 → 投递到线程池任务队列            │
│     ├─ EPOLLOUT → 写响应 (send_buf → write)             │
│     ├─ eventfd  → Worker 通知主线程某连接可写            │
│     └─ 超时     → timer_tick() 推动时间轮               │
│                                                         │
│   纯 I/O 操作，零阻塞                                    │
└──────────────────────┬──────────────────────────────────┘
                       │ 任务队列 (mutex + cond)
                       ▼
┌─────────────────────────────────────────────────────────┐
│                工作线程池 (CPU 核数 × 2)                   │
│                                                         │
│   Worker: while(1) {                                    │
│     task = dequeue();                                   │
│     handler(conn, frame);  // 可能包含:                  │
│       ├─ TLV 编解码                                     │
│       ├─ JWT 验签                                       │
│       ├─ VFS 路径解析 → MySQL 查询                      │
│       ├─ 存储引擎 → 文件 I/O                             │
│       └─ 构造响应 → eventfd 通知主线程                   │
│   }                                                     │
│                                                         │
│   处理阻塞操作 (DB、磁盘 I/O)                             │
└─────────────────────────────────────────────────────────┘
```

### 10.2 同步机制

| 结构 | 同步方式 | 说明 |
|------|----------|------|
| 任务队列 | `mutex` + `condition variable` | 主线程入队，Worker 出队 |
| MySQL 连接池 | `mutex` + `condition variable` | 无可用连接时 Worker 阻塞 |
| 时间轮 | `pthread_spinlock` | 临界区 <100ns，避免上下文切换 |
| LRU 缓存 | `pthread_rwlock` | 读并发 (95%)，写互斥 (5%) |
| 用户配额 | 乐观锁 (SQL WHERE) | `UPDATE ... WHERE used_bytes + delta <= quota_bytes` |
| 文件去重 | 事务 + 行锁 | `SELECT ... FOR UPDATE` |
| 上传会话 | `SELECT ... FOR UPDATE` | 同一 upload_id 的并发分片串行化 |

### 10.3 死锁预防

按固定顺序加锁：`timer_lock → db_pool_lock → cache_lock → file_lock`。

`db_acquire()` 超时 5 秒返回错误，避免永久阻塞。epoll 事件分发阶段完全无锁。


---

## 11. 配置设计

```ini
# netdisk.conf — 服务端配置模板

[server]
listen_host     = 0.0.0.0            # 监听地址
listen_port     = 8443               # 监听端口
max_connections = 10000              # 最大并发连接数
keepalive_timeout = 60               # 空闲超时 (秒)

[thread_pool]
worker_count    = 0                  # 工作线程数, 0=CPU核数×2
queue_size      = 4096               # 任务队列容量

[database]
host            = 127.0.0.1
port            = 3306
user            = netdisk
password        = changeme           # ⚠️ 部署前必须修改
database        = netdisk
pool_size       = 16                 # 连接池大小

[storage]
data_dir        = /data/netdisk/files
tmp_dir         = /data/netdisk/tmp
max_file_size   = 10737418240        # 单文件上限 10GB
chunk_size      = 4194304            # 分片大小 4MB
upload_session_timeout = 86400       # 上传会话过期 (24小时)

[cache]
path_cache_size = 10000              # LRU 路径缓存条目数
path_cache_ttl  = 60                 # 缓存 TTL (秒)

[security]
# ⚠️ 部署前必须更换！使用: openssl rand -base64 32
jwt_secret      = changeme-32-bytes-key
session_timeout = 60                 # JWT 过期时间 (秒) — 当前固定 900 秒

[log]
level           = info               # debug | info | warn | error
output          = file               # file | stdout | both
file_path       = /var/log/netdisk.log
max_size        = 104857600          # 100MB 自动轮转
```

---

## 12. 编译与部署

### 12.1 依赖

```bash
# CentOS / RHEL
yum install -y gcc cmake make mysql-devel zlib-devel

# Ubuntu / Debian
apt install -y gcc cmake make libmysqlclient-dev zlib1g-dev
```

SHA256 哈希使用自实现版本（`src/base/utils.c` → `sha256()`），不依赖 OpenSSL。CRC32 校验依赖 zlib（`crc32()`）。

### 12.2 客户端配置

客户端通过 `~/.netdisk.conf` 配置连接信息，零参数启动：

```ini
# ~/.netdisk.conf  — 客户端本地配置

[server]
host = 192.168.1.100        # 服务端地址
port = 8443                 # 服务端端口

[client]
connect_timeout = 10        # TCP 连接超时 (秒)
auto_reconnect  = yes       # 断线自动重连
download_dir    = ~/Downloads
multi_threads   = 4         # 多点下载默认线程数
```

**启动逻辑**：

```c
int main(int argc, char **argv) {
    client_config_t cfg;

    // 配置优先级: 命令行 -c 指定 > ~/.netdisk.conf > 内置默认值
    char *config_path = "~/.netdisk.conf";
    if (argc >= 3 && strcmp(argv[1], "-c") == 0)
        config_path = argv[2];
    client_config_load(config_path, &cfg);

    // 连接 → REPL
    int sockfd = cli_connect(cfg.host, cfg.port);
    repl_loop(sockfd);
    cli_disconnect(sockfd);
}
```

### 12.3 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
# 产出: netdisk-server + netdisk
```

### 12.4 运行

```bash
# 服务端 (守护进程模式)
./netdisk-server -c config/netdisk.conf -d

# 客户端 (零参数)
$ netdisk
Connecting to 192.168.1.100:8443... [OK]
NetDisk>
```

---

## 13. 后续版本规划

### P0 — 安全加固（应尽快实施）

| 功能 | 说明 | 工作量 |
|------|------|--------|
| 传输加密 (TLS) | 补充 TLS/SSL 层或 ECDH + AES-GCM | 约 200 行 |
| bcrypt/scrypt | 替换 SHA256+盐，增加密码破解成本 | 约 50 行 |

### P1 — 性能与核心体验

| 功能 | 说明 | 工作量 |
|------|------|--------|
| LRU 路径缓存 | 双向链表+哈希表，减少热路径 SQL | 约 300 行 |
| 全文搜索 | MySQL FULLTEXT + ngram 分词；`search.c` | 约 250 行 |
| 打包下载 | zlib 流式 zip + sendfile；`archive.c` | 约 300 行 |
| 分享链接 | `shares` 表 + 提取码；`share.c` | 约 200 行 |

### P2 — 生态与体验

| 功能 | 说明 | 工作量 |
|------|------|--------|
| WebDAV 协议 | 系统原生挂载为网络驱动器 | 约 500 行 |
| FUSE 客户端 | 虚拟文件系统挂载为本地目录 | 约 400 行 |
| 文件版本管理 | `version_of` 自引用链表 | 约 300 行 |

---

## 14. 工程量估算

### 14.1 服务端模块

| 模块 | 文件 | 估行 | 说明 |
|------|------|------|------|
| 全局类型 | `proto/types.h` | 80 | 帧结构、CMD 码、TLV Type 枚举 |
| 入口 | `main.c` | 100 | 参数解析、守护进程、信号处理 |
| 配置解析 | `base/config.c/h` | 250 | INI 格式，10 个配置段 |
| 日志系统 | `base/log.c/h` | 180 | 异步环形缓冲 + 文件轮转 |
| epoll 框架 | `net/server.c/h` | 450 | accept/read/write 非阻塞、conn 管理、eventfd |
| 时间轮 | `net/timer.c/h` | 180 | 60 槽位、O(k) 到期、自旋锁 |
| 协议层 | `proto/protocol.c/h` | 280 | 帧解析状态机 (5 状态)、帧构造 |
| TLV 编解码 | `proto/tlv.c/h` | 200 | 编码/解码、边界检查、类型校验 |
| 命令分发 | `route/dispatcher.c/h` | 80 | CMD→handler 跳转表 (14 条目) + 令牌桶 |
| 命令处理 | `route/handler.c/h` | 700 | 12 个 handler |
| 认证 | `route/auth.c/h` | 200 | register (加盐)、login (JWT 签发)、logout |
| JWT | `route/jwt.c/h` | 120 | HS256 生成/验证、Base64URL |
| 虚拟文件系统 | `biz/vfs.c/h` | 500 | 路径解析、mkdir/list/delete/move/create |
| 存储引擎 | `biz/storage.c/h` | 750 | 上传/下载/去重/秒传/位图/CRC32 |
| MySQL 连接池 | `data/db.c/h` | 300 | 预连接池、互斥锁+条件变量 |
| 线程池 | `base/thread_pool.c/h` | 130 | 环形队列、互斥锁+条件变量 |
| 工具函数 | `base/utils.c/h` | 180 | SHA256、Base64、MIME 检测 |
| **服务端小计** | | **≈4680** | |

### 14.2 客户端

| 模块 | 文件 | 估行 | 说明 |
|------|------|------|------|
| REPL 入口 | `client/main.c` | 300 | 命令行解析、读配置、交互循环 |
| 客户端协议 | `client/protocol.c/h` | 220 | 帧收发、阻塞读响应 |
| 命令实现 | `client/commands.c/h` | 500 | 12 个命令 + 进度条 + SHA256 |
| **客户端小计** | | **≈1020** | |

### 14.3 支撑模块

| 类别 | 文件 | 估行 | 说明 |
|------|------|------|------|
| 数据库脚本 | `sql/schema.sql` | 60 | 3 张表 DDL |
| 构建系统 | `CMakeLists.txt` (×2) | 80 | 顶层 + 子目录 |
| 配置文件 | `config/*.conf` | 40 | 服务端 + 客户端模板 |
| 单元测试 | `tests/test_*.c` | 250 | TLV、VFS、帧解析 |
| **支撑小计** | | **≈430** | |

### 14.4 汇总与排期

```
服务端  ████████████████████████████████████  4680 行  (76%)
客户端  ██████████                            1020 行  (17%)
支撑    ████                                  430 行  ( 7%)
────────────────────────────────────────────────────────
总计    ██████████████████████████████████████  6130 行
```

**6 周开发计划**：

| 周 | 重点 | 交付物 |
|----|------|--------|
| W1 | 基础设施 | `config` + `tlv` + `log` + `types.h` + `thread_pool` + `utils` |
| W2 | 协议与框架 | `protocol` + `dispatcher` + `server` + `timer` |
| W3 | 数据层 | `db` + `schema.sql` + `auth` + `jwt` |
| W4 | 业务层 ⚡ | `vfs` + `storage`（上传/下载/去重/秒传）← 最复杂周 |
| W5 | 命令层 | `handler`（12 个命令）+ 断点续传 |
| W6 | 客户端 | `REPL` + `commands` + 进度条 |

---

## 附录 A: 命令码速查表

| CMD | 名称 | 方向 | 一句话说明 |
|-----|------|------|-----------|
| `0x02` | `AUTH` | C→S | 登录 |
| `0x03` | `AUTH_OK` | S→C | 登录成功 |
| `0x04` | `AUTH_ERR` | S→C | 登录失败 |
| `0x07` | `BYE` | 双向 | 断开 |
| `0x08` | `REGISTER` | C→S | 注册 |
| `0x10` | `LS` | C→S | 列目录 |
| `0x11` | `CD` | C→S | 切换目录 |
| `0x12` | `MKDIR` | C→S | 建目录 |
| `0x13` | `RM` | C→S | 删除 |
| `0x14` | `MV` | C→S | 移动 |
| `0x15` | `PUT` | C→S | 上传 |
| `0x16` | `GET` | C→S | 下载 |
| `0x17` | `STAT` | C→S | 元信息 |
| `0x20` | `RESUME_PUT` | C→S | 续传上传 |
| `0x21` | `RESUME_GET` | C→S | 续传下载 |
| `0x80` | `OK` | S→C | 成功 |
| `0x81` | `ERR` | S→C | 通用错误 |
| `0x82` | `ERR_AUTH` | S→C | 认证失败 |
| `0x83` | `ERR_EXISTS` | S→C | 已存在 |
| `0x84` | `ERR_NOT_FOUND` | S→C | 不存在 |
| `0x85` | `ERR_QUOTA` | S→C | 配额超限 |
| `0x86` | `ERR_ACCESS` | S→C | 权限不足 |
| `0x87` | _(保留)_ | — | 预留 |
| `0x88` | `ERR_RANGE` | S→C | 偏移无效 |

## 附录 B: TLV Type 速查表

| Type | 名称 | Value | 用途 |
|------|------|-------|------|
| `0x01` | `PATH` | 字符串 | 路径 |
| `0x02` | `FILENAME` | 字符串 | 文件名 |
| `0x03` | `FILE_SIZE` | `uint64` | 文件大小 |
| `0x04` | `FILE_HASH` | `uint8[32]` | SHA256 |
| `0x05` | `MIME_TYPE` | 字符串 | MIME |
| `0x06` | `TOKEN` | 字符串 | JWT |
| `0x07` | `USERNAME` | 字符串 | 用户名 |
| `0x08` | `PASSWORD` | 字符串 | 密码 |
| `0x09` | `ERROR_MSG` | 字符串 | 错误描述 |
| `0x0A` | `FILE_ID` | `uint64` | node_id |
| `0x0B` | `PARENT_ID` | `uint64` | 父目录 ID |
| `0x0C` | `OFFSET` | `uint64` | 偏移 |
| `0x0D` | `CHUNK_SEQ` | `uint32` | 分片序号 |
| `0x0E` | `CHUNK_DATA` | 二进制 | 分片数据 |
| `0x0F` | `CRC32` | `uint32` | 校验 |
| `0x10` | `ACTION` | `uint8` | 子动作 |
| `0x11` | `UPLOAD_ID` | `uint64` | 上传 ID |
| `0x12` | `MSG` | 字符串 | 消息 |
| `0x13` | `ENTRY_TYPE` | `uint8` | 条目类型 |
| `0xFE` | `ENTRY_START` | — | 条目开始 |
| `0xFF` | `ENTRY_END` | — | 条目结束 |

## 附录 C: 错误码速查表

| CMD | 名称 | 触发条件 |
|-----|------|---------|
| `0x81` | `ERR` | 未分类的通用错误 |
| `0x82` | `ERR_AUTH` | Token 无效 / 过期 / 未登录 |
| `0x83` | `ERR_EXISTS` | 创建文件/目录时同名冲突 |
| `0x84` | `ERR_NOT_FOUND` | 路径不存在 / 文件已删除 |
| `0x85` | `ERR_QUOTA` | 上传后 `used_bytes` 超 `quota_bytes` |
| `0x86` | `ERR_ACCESS` | 操作其他用户的文件 |
| `0x88` | `ERR_RANGE` | `OFFSET` > 文件大小 |

---
END