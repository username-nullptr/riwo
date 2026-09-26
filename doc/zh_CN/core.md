# 核心模块

语言：[English](../en/core.md) | 简体中文

| 属性 | 值 |
| --- | --- |
| 构建 | 始终启用 |
| 源码树 Target | `riwo.core` |
| 安装 Target | `Riwo::core` |
| 聚合头 | `<riwo/core.h>` |
| 依赖 | 选定的 Asio provider；可选 OpenSSL/liburing |

Core 是其他 Riwo 模块的基础，负责公共运行时、值与容器、算法、文件、系统访问和
线程级同步。

## 头文件地图

| 区域 | 首选头文件 | 主要能力 |
| --- | --- | --- |
| 执行 | `<riwo/core/execution.h>` | 默认 context、调度、Timer、等待与异步任务 |
| 值 | `<riwo/core/value.h>`、`container.h`、字符串容器 | 文本值、转换与参数容器 |
| 结构化输入 | `<riwo/core/url.h>`、`ini.h`、`args_parser.h` | URL、INI 与命令行解析 |
| 算法 | `<riwo/core/algorithm.h>`、`mime_type.h` | UUID、SHA-1、通配/编码/数学辅助与 MIME |
| 同步 | `<riwo/core/atomic_mutex.h>`、`shared_mutex.h`、`lock_free_queue.h` | 线程锁与队列 |
| 线程生命周期 | `<riwo/core/jthread.h>` | 自动汇合线程、停止源/令牌/回调 |
| 系统 | `<riwo/core/system.h>` | 应用路径、环境、CPU 与动态库 |
| 语言支持 | `<riwo/core/cxx/...>`、`<riwo/core/utils/...>` | Concepts、traits、expected/optional、格式化与 Asio 适配 |

`<riwo/core.h>` 聚合常用数据、解析、算法、同步和系统头文件。Execution、value、
MIME、无锁队列等专用能力有独立入口，应直接包含对应头文件。

## 运行时

```cpp
#include <riwo/core/execution.h>

#include <chrono>

int main()
{
    using namespace std::chrono_literals;

    riwo::post(1s, [] { riwo::exit(); });
    return riwo::exec();
}
```

进程级默认运行时适合小型程序；调度重载也接受应用持有的 executor。completion
token、取消、生命周期、strand 和关闭顺序统一见
[执行与 I/O 模型](io-model.md)。

## 数据与输入

- `value` 保存文本，并提供受检查的转换和格式化。
- 参数与字符串容器提供上层模块共用的键值表达。
- `url` 解析并合并层次 URL；具体接受哪些 scheme 由协议模块决定。
- `ini` 提供 group/key 访问，以及同步和 token 化文件 I/O。
- `cmdline::args_parser` 处理别名、值选项、组合标志、位置参数、帮助和版本。
- 算法头文件提供 Hash、标识符、匹配、编码、字节序与数学辅助。
- MIME 辅助通过后缀和内容检查对文件分类。

## 线程与系统访问

线程允许阻塞时使用 Core 锁和队列；等待应挂起协程时使用
[协程](coroutines.md)原语。

`atomic_mutex` 与 `atomic_shared_mutex` 默认采用 balanced 策略。low-latency
策略会自旋，只适用于受控线程上的短小、有界临界区。

标准库可用时，`riwo::jthread`、`stop_token`、`stop_source` 和
`stop_callback` 使用标准实现，否则由 Core 提供兼容回退。

`riwo::app` 区域提供可执行文件、工作目录、主目录、用户和环境能力。
`riwo::library` 加载动态库及符号；使用任何已解析符号期间，都要保持动态库对象有效。

## 边界

Core 不负责协程感知锁、协议客户端/服务端、应用日志或进程管理，它们分别属于
Coroutines、HTTP、WebSocket 与 Utilities。OpenSSL 和 io_uring 会改变 Core/Asio
构建能力，但 TLS 策略和 executor 生命周期仍由应用负责。

可运行覆盖见 [Core 示例](../../examples/README.md#core)。
