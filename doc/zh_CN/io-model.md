# 执行与 I/O 模型

语言：[English](../en/io-model.md) | 简体中文

本契约适用于 Core、Coroutines、HTTP、WebSocket 与 Utilities。模块指南只增加
对象级规则，不会替代本页。

## 选择运行时

Riwo 暴露一个进程级默认运行时：

| API | 作用 |
| --- | --- |
| `riwo::io_context()` | 访问默认 `asio::io_context` |
| `riwo::get_executor()` | 访问其 executor |
| `riwo::exec()` | 运行到停止 |
| `riwo::exit(code)` | 停止运行时，并设置 `exec()` 返回的进程结果 |

感知 executor 的重载也接受应用持有的 Asio executor。简单的进程级事件循环可使用
默认运行时；需要隔离、明确线程位置或可控关闭顺序时，应使用自有 context、strand
或 thread pool。

executor 只回答“工作在哪里运行”。它不会自动持有任务捕获的全部对象，也不会让
I/O 对象自动变为线程安全。

## 选择完成方式

声明允许时，异步 API 使用 Asio completion token：

| Token/方式 | 结果模型 |
| --- | --- |
| `riwo::use_sync` 或同步默认值 | 调用返回前完成 |
| `riwo::use_awaitable` | 返回 `riwo::awaitable<T>`；I/O 错误转为 `std::system_error` |
| `riwo::use_future` | 返回 Future；I/O 错误转为异常 |
| `riwo::detached` | 调用方不观察完成值 |
| Callback token | 调用给定完成处理器 |
| `riwo::deferred` | 在支持的位置返回延迟操作 |

不是每个操作都接受全部 token，以函数声明为准。token 还可以携带关联 executor、
allocator、immediate executor、cancellation slot、错误重定向或超时。它们会影响
完成过程，但不会改变借用参数的生命周期。

异步发起函数通过正常完成路径交付即时失败；不要依赖 callback 在发起调用内部
同步执行。

## 持有数据直到完成

除非重载明确说明会复制或持有参数：

- Buffer、span、string view、引用、指针及其目标都是借用；
- I/O 对象和所有借用参数必须存活到完成；
- 取消只会启动完成过程，不会立即释放借用状态；
- detached 协程必须持有捕获状态，或确保每个引用都比协程存活更久；
- 父对象返回的对象可能依赖父对象、连接或 executor。

Detached response 写入和其他明确持有参数的重载属于例外，不是默认所有权规则。

## 串行化有状态对象

除非类型明确给出更强保证：

- 不同对象可以并发使用；
- 一个共享的有状态对象不会自动保证线程安全；
- 同一逻辑对象的发起、处理器、取消、关闭和销毁必须串行化；
- 如果处理器仍可并发执行，只给发起调用加互斥锁并不够。

多个线程运行同一个 `io_context` 时，把一个逻辑服务留在同一个 strand：

```cpp
asio::io_context context;
auto strand = asio::make_strand(context);
riwo::websocket::client client(strand);

asio::co_spawn(strand, [&]() -> riwo::awaitable<void>
{
    auto stream = co_await client.open(
        "ws://127.0.0.1:8080/echo", riwo::use_awaitable);

    // 后续对 client 和 stream 的访问也留在同一 strand。
    co_await stream.close(riwo::use_awaitable);
}, riwo::detached);
```

只把最终处理器绑定到 strand，不能追溯性地串行化其他访问同一对象的代码。
所有相关操作都应从该 strand 发起。

## 单对象操作限制

| 区域 | 同一对象的限制 |
| --- | --- |
| Core 调度 | `post` 和 `dispatch` 遵循传入的 executor；捕获状态仍由调用方负责 |
| 协程原语 | 支持多个等待者；原语必须比所有等待者存活更久 |
| HTTP connection | 最多一个读和一个写；二者可以重叠 |
| HTTP client/server/context | 状态转换和访问必须串行化 |
| WebSocket stream | 最多一个读族操作；一个读和一个写可以重叠；消息写入使用有界队列 |
| Process | 最多一个 stdin 写、一个 stdout 读和一个 stderr 读 |
| INI/settings 文件 I/O | 文件任务可以在工作 executor 上运行；可变对象状态仍需串行化 |
| Signal/软总线交付 | 交付可以排队，但 view、引用和 callback 捕获仍需保持有效 |

内部队列只表示某个已记录的操作会排队，不表示可以任意并发访问所属对象。

## 按顺序取消与关闭

对应用持有的运行时，按以下顺序关闭：

1. 停止发起新任务。
2. 取消或关闭活动的协议、Timer、Process 与等待操作。
3. 在完成事件排空期间保持对象和借用状态有效。
4. 最终完成后再销毁依赖对象。
5. 最后停止并汇合 executor 线程。

对进程级默认运行时，`riwo::exit()` 会停止事件循环，但不能代替应用层协议关闭、
Buffer 所有权和必须在事件循环停止前完成的清理。
