# 协程

语言：[English](../en/coroutines.md) | 简体中文

| 属性 | 值 |
| --- | --- |
| 构建选项 | `RIWO_BUILD_CORO=ON`（默认） |
| 源码树 Target | `riwo.coro` |
| 安装 Target | `Riwo::coro` |
| 聚合头 | `<riwo/coro.h>` |
| 依赖 | Core |

Coroutines 增加可挂起同步与 executor 切换辅助，是 HTTP 与 Utilities 的前置模块。

## 启动协程任务

`riwo::dispatch()` 与 `riwo::post()` 接受 awaitable，或返回 awaitable 的
可调用对象：

```cpp
#include <riwo/coro.h>

using namespace riwo::coro::literals;

int main()
{
    riwo::dispatch([]() -> riwo::awaitable<void>
    {
        co_await 250_ms;
        riwo::exit();
    });

    return riwo::exec();
}
```

时长字面量包括 `_y`、`_mon`、`_d`、`_h`、`_min`、`_s`、
`_ms`、`_us` 与 `_ns`。

## 功能

| 头文件/API | 作用 |
| --- | --- |
| `coro::mutex`、`coro::unique_lock` | 独占锁与 RAII 所有权 |
| `coro::shared_mutex`、`coro::shared_lock` | 共享与独占锁 |
| `coro::semaphore`、`coro::binary_semaphore` | 计数与二元许可 |
| `coro::condition_variable` | 谓词等待、超时等待与单个/全部通知 |
| `coro::wait()` | 等待 Future、Thread、`riwo::jthread` 或 Asio thread pool |
| `coro::goto_exec()` | 在另一 executor 恢复，并返回原 executor |
| `coro::goto_thread()` | 在工作线程恢复 |

带超时的锁和 semaphore 操作产生布尔型获取结果。

## 所有权与顺序

- 同步原语必须比所有已排队和已恢复的等待者存活更久。
- 修改 condition-variable 谓词时应持有其关联互斥锁。
- detached 协程必须持有无法证明具有更长生命周期的捕获值。
- 除非明确设计了跨 executor 顺序，否则不要持锁切换 executor。
- 停止负责恢复等待者的 executor 前，先取消或完成未结束等待。
- 只有协程等待者才使用协程原语；有意阻塞线程的临界区应使用 Core 线程锁。

共享的 executor、完成、取消、strand 与关闭契约见
[执行与 I/O 模型](io-model.md)。可运行用法见
[协程示例](../../examples/README.md#coroutines)。
