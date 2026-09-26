# Riwo 文档

语言：[English](../en/README.md) | 简体中文

文档直接对应当前项目：先理解模块图和构建边界，再阅读通用执行规则，
最后进入实际使用的模块。

## 按目标阅读

| 目标 | 起点 | 后续 |
| --- | --- | --- |
| 评估这个库 | [项目结构](architecture.md) | 下方对应的模块指南 |
| 构建或集成 | [构建与配置](build.md) | [示例](../../examples/README.md) |
| 编写异步代码 | [执行与 I/O 模型](io-model.md) | [协程](coroutines.md) |
| 使用网络协议 | [HTTP](http.md) 或 [WebSocket](websocket.md) | [执行与 I/O 模型](io-model.md) |
| 开发或验证 Riwo | [项目结构](architecture.md) | [测试](../../test/README.md) |

## 模块指南

| 模块 | 定位 | 指南 |
| --- | --- | --- |
| `riwo.core` | 通用数据、执行、算法、系统和同步能力 | [核心模块](core.md) |
| `riwo.coro` | 协程等待与同步 | [协程](coroutines.md) |
| `riwo.http` | HTTP/1.0 和 HTTP/1.1 客户端、服务端与协议工具 | [HTTP](http.md) |
| `riwo.websocket` | RFC 6455 客户端、服务端、Stream 和编解码 | [WebSocket](websocket.md) |
| `riwo.utils` | 日志、设置、Signal、进程、模块与软总线 | [应用工具](utilities.md) |

## 文档边界

- [`riwo/`](../../riwo) 中的公共声明是 API 的最终依据。
- [构建与配置](build.md)统一说明 CMake 开关、产物、安装与消费端 Target。
- [执行与 I/O 模型](io-model.md)统一说明 executor、生命周期、取消与并发；
  模块页只补充特例和单对象限制。
- [示例](../../examples/README.md)维护可执行程序名、参数和组合方式；
  [测试](../../test/README.md)维护验证配置和筛选命令。
- `detail/` 下的文件同时服务于实现和公共模板，不应作为应用的首选包含入口。
