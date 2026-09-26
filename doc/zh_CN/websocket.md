# WebSocket

语言：[English](../en/websocket.md) | 简体中文

| 属性 | 值 |
| --- | --- |
| 构建选项 | `RIWO_BUILD_WEBSOCKET=ON`（默认 OFF） |
| 源码树 Target | `riwo.websocket` |
| 安装 Target | `Riwo::websocket` |
| 聚合头 | `<riwo/websocket.h>` |
| 依赖 | HTTP |
| 可选能力 | `RIWO_OPENSSL_SUPPORT` 提供 WSS；WebSocket zlib 支持提供 `permessage-deflate` |

该模块实现 HTTP/1.1 上的 RFC 6455：打开握手、客户端、服务端、升级后的 Stream、
Frame/消息 I/O、关闭处理，以及显式打开重试。

## 头文件地图

| 区域 | 首选头文件 | 主要能力 |
| --- | --- | --- |
| 客户端 | `<riwo/websocket/client.h>` | WS/WSS 打开、重定向、Cookie、代理与诊断 |
| 服务端/升级 | `<riwo/websocket/server.h>` | 自有 Listener 与从 HTTP 服务升级 |
| Stream | `<riwo/websocket/stream.h>` | 消息/Frame I/O、控制帧、关闭与生命周期 |
| 重试 | `<riwo/websocket/retry.h>` | 按策略重试失败的打开操作 |
| 协议 | `<riwo/websocket/protocol/...>` | 握手与 Frame 编解码 |
| 配置 | `<riwo/websocket/types.h>` | 消息、关闭、Stream 限制、保活与压缩 |

聚合头包含高层客户端、服务端与重试入口。使用 Stream、类型或协议底层时，应直接
包含对应头文件。

## 打开连接

`websocket::client` 持有一个 HTTP client，`client.open()` 通过该 client
打开连接。自由函数 `websocket::open(http_client, ...)` 会复用应用持有的 HTTP
client，包括 connector、Cookie、连接池与代理策略。

`connect_request` 配置请求 Header/认证、代理选择、Stream 限制、握手超时、
重定向、子协议与扩展。默认代理策略依次检查 `ws_proxy`/`wss_proxy`、对应的
HTTP/HTTPS 变量和 `all_proxy`，并遵循 `no_proxy`。

一次 `open()` 只执行一次打开尝试。`retry_open()` 可以按指数退避、上限、抖动、
次数限制、决策回调和观察器创建新尝试。它不会监控已建立的 Stream、重放消息、
恢复认证/订阅，也不运行业务接收循环。

## 接受连接或升级

`websocket::server` 持有 HTTP listener 并执行打开握手。每个 server 只选择一种
交付方式：

- 显式调用 `accept()`；或
- 注册 `on_connection()`/`on_default()` 处理器。

混合 HTTP/WebSocket 服务先用 `http::server` 路由，检查
`is_upgrade_request()`，再调用 `websocket::upgrade()`。成功结果包含 Stream、
不可变请求快照及选中的子协议/扩展信息。

`upgrade_options` 控制 Stream 限制、握手超时、响应 Header、协议/扩展白名单，
以及同步或 awaitable 验证器与选择器。同步 `upgrade()` 不能运行 awaitable 回调。

## 使用 Stream

| 操作 | 结果 |
| --- | --- |
| `read<Buffer>()` | 一个完整文本或二进制消息 |
| `consume()` | 通过临时分块交付一个消息 |
| `read_frame<Buffer>()` | 保留 Frame 边界的一个数据帧 |
| `write_text()`、`write_binary()`、`write()` | 排队一个完整消息 |
| `write_frame()` | 写入显式分片的数据帧 |
| `ping()`、`pong()` | 发送手动控制帧 |
| `close()` | 执行 RFC 关闭握手 |
| `shutdown()` | 立即关闭传输 |
| `wait_written()` | 观察已接受的排队写入 |
| `wait_closed()`、`on_closed()` | 观察最终关闭信息 |

同一 Stream 的规则：

- `read()`、`consume()`、`read_frame()` 中最多一个处于活动状态；
- 一个读和一个写可以重叠；
- 消息写入通过受操作数和字节数限制的队列串行化；
- `message_chunk::body` 只在对应 `consume()` 回调期间有效；
- 控制回调由活动读处理控制帧时执行；
- 完整消息读取会组合 continuation frame 并检查消息限制；
- 多线程运行事件循环时，所有访问必须留在同一 strand。

`stream_config` 控制 Frame/消息限制、Buffer、分片、排队写入、关闭超时、保活与
压缩。正数 `ping_interval` 会启用自动 Ping/Pong，并要求活动读处理 Pong；
只有该间隔为零时才能手动调用 `ping()`/`pong()`。

## 压缩与边界

双方必须协商 `permessage-deflate`。`permessage_deflate_options` 控制协商策略，
`compression_config` 控制自动消息压缩和阈值。

该模块覆盖掩码、分片、UTF-8 校验、控制帧、WS/WSS、重定向、Cookie、子协议、
HTTP/SOCKS5 代理、超时、取消与有界写入。不提供 HTTP/2 或 HTTP/3 上的
WebSocket、`permessage-deflate` 之外的扩展、持久重放或应用路由。

共享生命周期与并发契约见[执行与 I/O 模型](io-model.md)。客户端、服务端、混合
Upgrade、重试、代理、Codec 与 WSS 程序见
[WebSocket 示例](../../examples/README.md#websocket)。
