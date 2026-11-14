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
| Process | `<riwo/system/process.h>` | 子进程生命周期、异步等待和标准流 I/O |

环境变量值使用 UTF-8 字符串。变量不存在用空 `std::optional` 表达；系统调用失败仍由
`sys_expected` 返回错误。

`system::process` 提供 start/run、join、detach、terminate、kill、取消、超时、
子进程工作目录/环境、单实例锁，以及 stdin/stdout/stderr I/O。I/O 未完成时保持
Process 对象有效；生命周期变化与 I/O 发起必须串行化，同一进程最多保留一个写、
一个 stdout 读和一个 stderr 读。

相对运行程序目录的路径使用 `<riwo/core/executable.h>` 中的
`riwo::executable::resolve()`。只有明确接受用户 Home 语法的 API 才使用
`system::expand_user_path()`。

可运行覆盖见 [System 示例](../../examples/README.md#system)。
