# NetDisk

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-blue.svg)]()

> 基于自定义二进制协议（NDP-Lite）的类 CLI 网络磁盘系统 — 纯 C 实现，epoll 高并发，SHA256 秒传去重

---

## 30 秒了解 NetDisk

```console
$ ./netdisk-client connect 127.0.0.1:9000
netdisk> register alice secret123
[OK] User 'alice' registered successfully.

netdisk> login alice secret123
[OK] Logged in as 'alice'.

netdisk> upload ./report.pdf
[OK] Uploaded report.pdf (2.3 MB, 0:01 elapsed, SHA256 matched)

netdisk> download report.pdf
[OK] Downloaded report.pdf → ./report.pdf (2.3 MB, 0:01 elapsed)
```

---

## 为什么选择 NetDisk

| 特性 | NetDisk | 同类方案 |
|------|---------|----------|
| **自定义协议 NDP-Lite** | 22 B/请求 | HTTP ~200 B/请求 |
| **SHA256 秒传** | 重复文件零传输，直接去重 | 需完整上传 |
| **CLI 友好** | 类 Linux 命令，零学习成本 | GUI 依赖或复杂 API |

- **自定义协议 (NDP-Lite)**：每个请求仅 22 字节帧头，相比 HTTP 请求（约 200 字节）节省近 90% 的协议开销，尤其适合局域网高吞吐场景。
- **SHA256 秒传去重**：上传前先计算文件 SHA256 摘要，服务端若已有相同文件则直接秒传完成，重复文件零数据传输。
- **CLI 友好**：完全模拟 Linux 命令风格（`upload`、`download`、`ls`、`rm`），无需学习新语法或 GUI 操作。

---

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/your-org/netdisk.git
cd netdisk

# 构建
mkdir build && cd build
cmake ..
make -j$(nproc)

# 启动服务端
./netdisk-server -p 9000

# 新终端，连接客户端
./netdisk-client connect 127.0.0.1:9000
```

---

## 文档导航

| 文档 | 说明 |
|------|------|
| [docs/index.md](index.md) | 文档导航中心 |
| [docs/explanation/architecture.md](explanation/architecture.md) | 系统架构设计 |
| [docs/reference/protocol/frame-format.md](reference/protocol/frame-format.md) | NDP-Lite 协议帧格式规范 |
| [docs/reference/cli-commands.md](reference/cli-commands.md) | CLI 命令速查 |

---

## 项目结构

```
netdisk/
├── src/
│   ├── server/          # 服务端核心 — epoll 事件循环、连接管理
│   ├── client/          # 客户端 — CLI 交互、命令解析
│   ├── net/             # 网络层 — Socket 封装、I/O 多路复用
│   ├── proto/           # 协议层 — NDP-Lite 编解码、TLV 序列化
│   ├── route/           # 路由层 — 请求分流、中间件、认证
│   ├── biz/             # 业务层 — 虚拟文件系统、秒传、存储引擎
│   ├── data/            # 数据层 — SQLite 数据库访问
│   └── base/            # 基础组件 — 日志、线程池、配置解析
├── include/             # 公共头文件
├── tests/               # 单元测试与集成测试
├── docs/                # 项目文档
├── CMakeLists.txt       # CMake 构建配置
├── DESIGN.md            # 设计文档
├── REPORT.md            # 项目报告
└── TASKS.md             # 任务跟踪
```

---

## 贡献

欢迎提交 Issue 和 Pull Request。详细信息请参阅 [CONTRIBUTING.md](../contributing/CONTRIBUTING.md)。

---

## 许可证

本项目基于 [MIT License](https://opensource.org/licenses/MIT) 开源。
