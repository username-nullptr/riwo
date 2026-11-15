# Crypto 模块

语言：[English](../en/crypto.md) | 简体中文

| 属性 | 值 |
| --- | --- |
| 构建选项 | `RIWO_BUILD_CRYPTO`（默认 ON） |
| 源码树 Target | `riwo.crypto` |
| 安装 Target | `Riwo::crypto` |
| 聚合头 | `<riwo/crypto.h>` |
| 依赖 | Core；Windows BCrypt |

Crypto 负责 HTTP 与 WebSocket 共用的密码学和标识符基础能力，使这些能力不再属于
Core 运行时边界。

## 头文件地图

| 头文件 | 能力 |
| --- | --- |
| `<riwo/crypto/base64.h>` | 文本和字节 Span 的 Base64 编码 |
| `<riwo/crypto/secure_random.h>` | 操作系统安全随机字节 |
| `<riwo/crypto/sha1.h>` | 增量 SHA-1，以及 Hex/Base64 输出 |
| `<riwo/crypto/uuid.h>` | UUID 解析与版本 4、5、6、7 生成 |

公共函数和类型位于 `riwo::crypto` 命名空间。直接使用这些头文件时，应链接
`riwo.crypto` 或 `Riwo::crypto`。

SHA-1 仅用于 WebSocket 握手、UUID v5 等兼容协议，不应在新的抗碰撞安全设计中使用。

Percent 编解码和通配匹配属于 [Core](core.md) 的文本基础能力，不属于 Crypto。
