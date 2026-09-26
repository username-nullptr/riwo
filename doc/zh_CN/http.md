# HTTP

语言：[English](../en/http.md) | 简体中文

| 属性 | 值 |
| --- | --- |
| 构建选项 | `RIWO_BUILD_HTTP=ON`（默认 OFF） |
| 源码树 Target | `riwo.http` |
| 安装 Target | `Riwo::http` |
| 聚合头 | `<riwo/http.h>` |
| 依赖 | Coroutines |
| 可选能力 | `RIWO_OPENSSL_SUPPORT` 提供 HTTPS；`RIWO_HTTP_ZLIB_SUPPORT` 提供 gzip |

该模块实现 HTTP/1.0 与 HTTP/1.1 客户端、服务端和协议构件，不实现 HTTP/2 或
HTTP/3。

## 头文件地图

| 区域 | 首选头文件 | 主要能力 |
| --- | --- | --- |
| 客户端 | `<riwo/http/client.h>` | 请求、Reply、Cookie、连接池、Connector 与代理 |
| 服务端 | `<riwo/http/server.h>` | Listener、路由、Request/Response、中间件与 Session |
| 协议 | `<riwo/http/protocol/...>` | 值、Header、Cookie、解析器、生成器、Range 与 Form |
| 传输 | `<riwo/http/utils/...>` | TCP/TLS Connection 与 I/O option token |

聚合头包含客户端与服务端入口。使用底层协议或传输能力时，应直接包含对应头文件。

## 客户端生命周期

一个高层请求分为三个明确阶段：

1. 通过 `request_get()`、`request_post()` 或其他方法发起请求。
2. 对返回的请求上下文调用 `wait_reply()`。
3. 读取、消费或保存响应体。

这些高层操作默认使用同步 token。协程用法需要在每个异步阶段传入
`riwo::use_awaitable`，也可以使用兼容的 callback/future token。要让池中连接保持
可复用，应在释放请求上下文前完整消费响应体。

| 配置 | 控制内容 |
| --- | --- |
| `request_arg` | Header、Cookie、Chunk 属性、Basic/Bearer 认证与代理认证 |
| `client::req_info` | URL、参数、代理选择、重定向与解压 |
| `client_config` | 默认代理与 `TCP_NODELAY` |
| `connection_pool_config` | 连接池大小与连接生命周期 |
| `connector` | 直连、TLS、HTTP CONNECT、SOCKS5 或自定义连接建立 |

全局代理策略读取大小写形式的
`http_proxy`/`https_proxy`/`all_proxy`，并遵循 `no_proxy`。单个请求可以继承、
绕过或指定 forward/tunnel 代理。

Body 传输辅助返回响应体字节数，不计 Header、Chunk framing 与 multipart 边界。

## 服务端生命周期

`http::server` 持有 acceptor，并按方法和路径路由请求。路径规则支持字面文本、
`*`、`?` 与命名 `{arguments}`。

| 区域 | 主要 API |
| --- | --- |
| 路由 | `on_request()`、`on_default()` |
| 错误处理 | `on_server_error()`、`on_service_error()` |
| 中间件 | `basic_aop`、`basic_ctrlr_aop` |
| Request | 方法/版本/target、Header、Cookie、参数、Body 与文件读取 |
| Response | 状态、Header、Cookie、定长/Chunk 写入、重定向、文件与 Range |
| Session | `service_context::session()`、`session_or()`、session manager |

`server_config::resource_root` 用于解析文件 API 的相对路径。它只是便捷根目录，
不是安全沙箱；任何来自请求数据的路径都必须由应用验证并限制。

协议升级时，`service_context::hand_over_connection()` 把连接和待处理字节移出
HTTP 生命周期。WebSocket 服务通常应使用
[`websocket::upgrade()`](websocket.md)，由它验证并完成 RFC 6455 握手。

## 操作契约

同一 connection 最多同时有一个读和一个写，二者可以重叠。Request/reply 阶段变化，
以及每个 client、server、request context 或 reply 对象的访问必须串行化。多个线程
运行事件循环时，同一服务的全部操作都应从同一 strand 发起。

非 detached 异步写会借用 Buffer 到完成；明确记录为 detached/持有参数的重载会保留
副本。取消不会在最终完成前结束这段生命周期。

HTTPS 使用应用配置的 `asio::ssl::context`。Riwo 提供 TLS 传输集成，但不选择
证书、验证或信任策略。

完整通用契约见[执行与 I/O 模型](io-model.md)。客户端、服务端、代理、文件、
Session、Codec 与 HTTPS 的可运行程序见
[HTTP 示例](../../examples/README.md#http)。
