# 项目结构

语言：[English](../en/architecture.md) | 简体中文

本页映射当前仓库和模块边界，只描述现状，不记录迁移历史。

## 分层与依赖

```text
riwo.core
└── riwo.coro
    ├── riwo.http
    │   └── riwo.websocket
    └── riwo.utils
```

| 层 | 职责 | 允许依赖 |
| --- | --- | --- |
| Core | Asio 适配、执行、通用值、算法、文件、系统与线程原语 | 选定的 Asio provider；可选 OpenSSL/liburing |
| Coroutines | 可挂起锁、条件变量、信号量、等待与 executor 切换 | Core |
| HTTP | HTTP/1.x 值、编解码、连接、客户端、服务端、路由与 Session | Coroutines；可选 zlib |
| WebSocket | RFC 6455 握手、Frame 编解码、客户端、服务端、Stream 与重试 | HTTP；可选 zlib |
| Utilities | 日志、设置、Signal、Observer、进程、模块启动与软总线 | Coroutines；spdlog |

依赖只能向下。HTTP 与 Utilities 是同层兄弟模块，WebSocket 是唯一建立在
HTTP 之上的库模块。启用上层模块时必须保留下层模块，CMake 会传递这些依赖。

## 源码树

| 路径 | 内容 | 规则 |
| --- | --- | --- |
| `riwo.h` | 感知构建配置的总聚合头 | 包含 Core 和所有已启用的可选模块 |
| `riwo/<module>.h` | 模块聚合头 | 便捷入口，不穷举专用 API |
| `riwo/<module>/` | 公共声明、模板支持与实现 | 边界与同名 CMake Target 一致 |
| `cmake/` | 项目选项、平台策略、Target 创建、测试与 package 导出 | 配置行为的最终依据 |
| `3rd_party/` | 随附 Asio 和 spdlog | provider 边界，不是 Riwo API |
| `examples/<module>/` | 小型可运行程序 | 每个可执行程序聚焦一个概念或流程 |
| `test/functional/` | 确定性 API 契约 | 默认行为验证 |
| `test/cmake/` | 配置与安装消费端契约 | 验证集成边界 |
| `test/stress/` | 并发与重复生命周期压力 | 按需启用 |
| `test/fuzz/` | 输入和操作序列探索 | 独立 Clang/libFuzzer 构建 |
| `test/performance/` | 不设固定阈值的性能测量 | 按需启用的 Release 构建 |
| `doc/en/`、`doc/zh_CN/` | 概念与集成指南 | 按主题和源码模块镜像 |

## 公共入口

应用通常包含所需的最窄公共头文件。不在意编译时间和包含广度时，可使用模块聚合头；
对已启用所有必需模块的小型应用，可直接使用 `<riwo.h>`。

源码树消费者链接 `riwo.<module>`；安装包消费者请求对应 component，并链接
`Riwo::<module>`：

| 模块 | 源码树 | 安装包 |
| --- | --- | --- |
| Core | `riwo.core` | `Riwo::core` |
| Coroutines | `riwo.coro` | `Riwo::coro` |
| HTTP | `riwo.http` | `Riwo::http` |
| WebSocket | `riwo.websocket` | `Riwo::websocket` |
| Utilities | `riwo.utils` | `Riwo::utils` |

`riwo.asio` 和 `riwo.spdlog` 等 provider Target 用于携带构建需求。应用应链接模块
Target，不应直接依赖 provider Target。

## 横切契约

模块划分不会取代通用运行时契约：

- 感知 executor 的任务遵循 Asio 的 executor 与 completion-token 模型；
- 借用的 Buffer 和引用状态必须存活到操作完成；
- 除非声明给出更强保证，否则同一有状态 I/O 对象的访问必须串行化；
- TLS、压缩和传输能力在配置 Riwo 时确定，并由生成的配置头文件暴露。

在多线程间共享 I/O 对象前，先阅读[执行与 I/O 模型](io-model.md)。选择模块、
provider、TLS、压缩或安装边界前，先阅读[构建与配置](build.md)。

## 变更应放在哪里

| 变更 | 主要位置 | 文档/验证 |
| --- | --- | --- |
| 公共 API 行为 | 对应 `riwo/<module>/` 区域 | 模块指南与功能覆盖表 |
| 模块或特性依赖 | 拥有该选项的 CMake 文件 | 构建指南与 CMake 测试 |
| 可运行使用场景 | `examples/<module>/` | 示例索引 |
| 确定性契约 | `test/functional/<module>/` | 功能覆盖表 |
| 压力、异常输入或测量 | Stress、Fuzz 或 Performance 套件 | 测试指南 |

这样每条事实只由一处维护，其他页面通过链接引用。
