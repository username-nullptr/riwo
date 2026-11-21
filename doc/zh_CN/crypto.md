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
| `<riwo/crypto/base64.h>` | Base64 和 Base64URL 编解码 |
| `<riwo/crypto/secure_random.h>` | 操作系统安全随机字节 |
| `<riwo/crypto/sha1.h>` | 增量 SHA-1，以及 Hex/Base64 输出 |
| `<riwo/crypto/sha256.h>` | 增量 SHA-256，以及原始字节、Hex 和 Base64 输出 |
| `<riwo/crypto/hmac_sha256.h>` | HMAC-SHA256 标签生成与校验 |
| `<riwo/crypto/uuid.h>` | UUID 解析与版本 4、5、6、7 生成 |

公共函数和类型位于 `riwo::crypto` 命名空间。直接使用这些头文件时，应链接
`riwo.crypto` 或 `Riwo::crypto`。

`base64_decode` 和 `base64url_decode` 将解码后的字节放在 `optional<std::string>` 中。
合法的空输入返回空字符串，格式错误返回空的 optional。两者都拒绝空白字符，并忽略
最后一个编码字符中未使用的位。标准 Base64 要求末尾分组使用规定的填充，Base64URL
解码接受带填充和无填充两种形式。调用 `base64url_encode(input, true)` 可保留填充。
[RFC 4648](https://www.rfc-editor.org/rfc/rfc4648.html)

`sha256` 支持分块 `append`。完成前可用 `digest()`、`hex()` 或 `base64()` 读取当前
摘要并继续追加。`finalize()` 可以重复调用，完成后再次追加会抛出 `riwo::logic_error`。

`hmac_sha256(key, message)` 一次性计算 32 字节标签，`hmac_sha256_verify(key, message, tag)`
接收完整的 32 字节标签，并以常量时间比较其内容。SHA-256 和 HMAC-SHA256 在默认构建中
可用，所需依赖见上表。[RFC 6234](https://www.rfc-editor.org/rfc/rfc6234.html)

SHA-1 仅用于 WebSocket 握手、UUID v5 等兼容协议，不应在新的抗碰撞安全设计中使用。

Percent 编解码和通配匹配属于 [Core](core.md) 的文本基础能力，不属于 Crypto。
