# System 模块

语言：[English](../en/system.md) | 简体中文

| 属性 | 值 |
| --- | --- |
| 构建选项 | `RIWO_BUILD_SYSTEM=ON`（默认 ON） |
| 源码树 Target | `riwo.system` |
| 安装 Target | `Riwo::system` |
| 聚合头 | `<riwo/system.h>` |
| 依赖 | Core |

System 负责带资源生命周期或进程级状态的可选操作系统能力。可执行文件相对路径解析
保留在 Core，其他模块不必仅为了定位项目资源而依赖 System。

## 头文件地图

| 区域 | 首选头文件 | 主要能力 |
| --- | --- | --- |
| 环境 | `<riwo/system/environment.h>` | 查询、枚举、设置和删除环境变量 |
| 用户 | `<riwo/system/user.h>` | 当前用户、Home 目录和 `~` 展开 |
| 应用目录 | `<riwo/system/paths.h>` | 定位配置、数据、缓存和状态目录 |
| 子进程 | `<riwo/system/process.h>` | 子进程生命周期、等待、输出捕获和标准流重定向 |
| 临时资源 | `<riwo/system/temporary.h>` | 排他创建临时文件和目录，析构时清理 |
| 文件锁 | `<riwo/system/file_lock.h>` | 跨进程非阻塞独占锁 |
| 目录监听 | `<riwo/system/directory_watcher.h>` | 同步或异步等待直属目录项变化 |

环境变量值使用 UTF-8 字符串。变量不存在用空 `std::optional` 表达；系统调用失败仍由
`sys_expected` 返回错误。

`system::process` 提供 start/run、join、detach、terminate、kill、取消、超时、
子进程工作目录/环境、单实例锁，以及 stdin/stdout/stderr I/O。I/O 未完成时保持
Process 对象有效；生命周期变化与 I/O 发起必须串行化，同一进程最多保留一个写、
一个 stdout 读和一个 stderr 读。

## 捕获子进程输出

```cpp
auto result = riwo::system::process::capture("git", {"--version"});
if (result && result->exit_code == 0)
    std::cout << result->stdout_data;
```

`capture()` 同步执行程序，并发读取 stdout 和 stderr，启动后立即关闭 stdin。
参数数组保留空参数和空格。非零退出码仍是有效的进程结果。
`process_capture_options` 默认超时 30 秒，各保留最多 1 MiB 的输出。
达到上限后继续读取相应管道，并设置该流的截断标记。将超时设为零可无限等待。
超时时终止直接子进程并回收资源，返回 `std::errc::timed_out`。

使用 `set_stdio(process_stdio)` 可分别为后续启动设置 `input`、`output` 和 `error`。
默认模式为 `pipe`，也可选择 `inherit`、`discard` 或 `file`。文件路径相对于父进程的
当前工作目录。输入文件需已存在，输出文件会创建或截断。两路输出指定相同文件路径时共享
同一个打开的文件。最后一次管道写入完成后，调用 `close_stdin()` 向子进程发送 EOF。

## 目录与文件

- `app_paths("org.example.tool")` 返回 `config`、`data`、`cache`、`state` 四个路径，调用方可按需创建目录。Windows 使用 Roaming AppData 保存配置，其余使用 Local AppData。Linux 使用对应的 XDG 变量，忽略相对路径，并在变量未设置时使用 Home 下的默认目录。应用 ID 以 ASCII 字母或数字开头，其余字符可用 ASCII 字母、数字、点、下划线和连字符，且不可用点结尾或使用 Windows 保留设备名。
- `temp_file::create()` 和 `temp_directory::create()` 在系统临时目录或指定的现有父目录中排他创建资源。对象可移动，析构时尝试清理。需要检查删除结果时调用 `remove()`，需要保留资源时调用 `release()`。
- `file_lock::try_lock(path)` 返回含有锁对象的结果、表示锁被占用的空 `std::optional`，或系统错误。调用 `unlock()` 或析构会释放锁。锁文件会保留，参与协调的进程应使用同一个路径，持锁期间应避免删除或替换锁文件。

## 等待目录变更

`directory_watcher<Exec>::open(executor, path)` 获取初始快照。也可以传入左值
执行上下文，例如 `open(context, path)`。传入的执行器或上下文提供的执行器须能
构造 `Exec`，默认类型为 `asio::any_io_executor`。
`get_executor()` 返回 `Exec`。若 Riwo 的执行器可用于构造 `Exec`，
也可以用 `open(path)`。

`wait()` 等到一批带绝对路径的
`created`、`modified` 或 `removed` 事件。监听直属目录项，能识别同名文件替换。
默认轮询间隔为 100 毫秒，轮询之间的瞬时变化可能合并或不可见。

- 同步等待在调用线程扫描，可以在 executor 尚未运行时使用。`wait()` 和 `wait(riwo::use_sync)` 用 `std::system_error` 报告错误。`wait(error_code)`、`as_tuple(use_sync)` 和 `as_expected(use_sync)` 提供错误码、元组或 `sys_expected` 结果。
- 同步等待接受 `std::chrono::duration` 或 `std::chrono::time_point`。零时长表示不限时。负时长或已过期的截止时间只扫描一次。
- `wait(token)` 也接受 callback、`use_awaitable`、`use_future`、`deferred`、`detached` 及异步错误重定向、超时和取消适配器。异步扫描在指定 executor 上进行。 [执行与 I/O 模型](io-model.md)

每个对象支持一个活动等待。同步和异步等待共用上一次成功交付的快照。
`cancel()` 可从另一线程唤醒阻塞的同步等待，也可取消异步等待。取消完成后可再次等待。

## 路径解析

相对运行程序目录的路径使用 `<riwo/core/executable.h>` 中的
`riwo::executable::resolve()`。只有明确接受用户 Home 语法的 API 才使用
`system::expand_user_path()`。

可运行覆盖见 [System 示例](../../examples/README.md#system)。
