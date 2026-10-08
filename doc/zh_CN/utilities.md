# 应用工具

语言：[English](../en/utilities.md) | 简体中文

| 属性 | 值 |
| --- | --- |
| 构建选项 | `RIWO_BUILD_UTILITIES=ON`（默认 OFF） |
| 源码树 Target | `riwo.utils` |
| 安装 Target | `Riwo::utils` |
| 聚合头 | `<riwo/utils.h>` |
| 依赖 | Coroutines 与 spdlog |
| 可选能力 | `RIWO_BUILD_UTILITIES_SBUS_UDP` 提供 UDP 软总线 |

Utilities 包含不属于协议栈的应用层能力。

## 头文件地图

| 区域 | 首选头文件 | 主要能力 |
| --- | --- | --- |
| 日志 | `<riwo/utils/logger.h>` | 命名控制台与轮转文件日志 |
| 设置 | `<riwo/utils/settings.h>` | 命名 INI 设置与变更 Signal |
| Signal | `<riwo/utils/signal_slot.h>` | 同步、异步与背压交付 |
| Observer | `<riwo/utils/observer.h>` | 由 executor 分派、按 ID 寻址的回调 |
| Modules | `<riwo/utils/modules.h>` | 按依赖顺序初始化应用模块 |
| Process | `<riwo/utils/process.h>` | 子进程生命周期与标准流 I/O |
| 软总线 | `<riwo/utils/sbus.h>` | 带类型的发布/订阅与 Topic 缓存 |

聚合头包含 logger、settings、modules 与软总线。使用 process、signal/slot 或
observer 时直接包含对应头文件。

## 日志与设置

`utils::logger` 管理命名控制台和文件 logger；配置覆盖路径、级别、轮转、时间戳与
格式。文件 sink 异步写入；需要在继续或退出前确认落盘时，调用
`logger::flush()`。

`utils::settings` 把 `riwo::ini` 包装为命名实例，并暴露 `changed` 与
`loaded` Signal。通过 `get()`/`set()` 访问值，需要直接控制持久化时使用
`ini()`。

## Signal、Observer 与模块启动

`utils::signal<Signature>` 支持三种交付模式：

| 模式 | 交付方式 |
| --- | --- |
| `sync` | 在调用方执行 slot |
| `async` | 在 executor 上排队 |
| `backpressure` | 在 executor 上排队，并阻塞到交付完成 |

不要从负责执行 slot 的同一个 executor 线程发起 backpressure 交付。分派会持有值
参数，但 view、指针、引用与 slot 捕获对象仍由调用方负责。

`utils::observer` 按稳定对象 ID 路由索引回调，并在析构时注销。
`utils::modules` 注册命名初始化器，按声明的依赖排序，并支持同步或异步初始化。

## 进程

`utils::process` 提供 start/run、join、detach、terminate、kill、取消、超时、
工作目录/环境、单实例锁，以及 stdin/stdout/stderr I/O。命令和参数使用
`std::filesystem::path`。

I/O 未完成时保持 Process 对象有效。生命周期变化与 I/O 发起必须串行化；同一进程
最多保留一个 stdin 写、一个 stdout 读和一个 stderr 读。

## 软总线

软总线把带类型的发布/订阅/缓存 API 与传输层分开：

| 传输 | 类型 | 可用条件 |
| --- | --- | --- |
| 进程内 | `local_interface`、`local_subscriber`、`local_cache` | Utilities 内始终可用 |
| UDP 多播 | `udp_interface`、`udp_subscriber`、`udp_cache` | `RIWO_BUILD_UTILITIES_SBUS_UDP=ON` |
| 自定义 | `basic_subscriber<Interface>`、`cache<Subscriber>`、`publish<Interface>()` | 用户实现 |

`RIWO_UTILS_SBUS_DEFAULT_INTERFACE` 为未限定 API 选择 `local`（默认）或
`udp`。行为不能随构建配置变化时，应在代码中显式写出传输类型。

`cache::wait_changed()` 等待发起后的下一次变化。它是边沿触发的，不回放此前变化；
持续订阅使用 `changed()`。`cache::cancel()` 只取消当前等待，不会断开持续订阅，
也不会阻止后续等待。

UDP 传输带版本、分片、限速，并限制来源跟踪、重组和 callback 交付队列；支持进程、
LAN 与路由多播范围。交付为 best-effort：过载保护可能丢弃流量，也不提供认证、
加密或可靠重放。网络策略与 ACL 应在 Riwo 外配置。

`utils::thread_pool()` 返回模块共享的 Asio thread pool。需要隔离或确定性关闭顺序
时，应使用应用持有的 executor。

共享所有权与并发契约见[执行与 I/O 模型](io-model.md)。日志、设置、Signal、
Observer、Process、Modules 与总线程序见
[Utilities 示例](../../examples/README.md#utilities)。
