# 构建与配置

语言：[English](../en/build.md) | 简体中文

本页是当前项目的 CMake 参考。测试专用开关和构建配置见
[测试指南](../../test/README.md)。

## 要求

| 组件 | 最低版本 |
| --- | --- |
| CMake | 3.15 |
| 语言模式 | C++20 |
| GCC | 13 |
| Clang | 17 |
| MSVC | 19.30（Visual Studio 2022） |

仓库随附 standalone Asio 和 spdlog。只有启用对应能力时，才会查找 OpenSSL、
zlib 和 liburing。

## 选择构建配置

默认库只包含 Core 与 Coroutines：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

构建全部库模块和适用示例：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DRIWO_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

TLS 和压缩是独立能力。例如：

```sh
cmake -S . -B build-secure -DCMAKE_BUILD_TYPE=Release \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_OPENSSL_SUPPORT=ON \
  -DRIWO_HTTP_ZLIB_SUPPORT=ON
cmake --build build-secure --parallel
```

## 模块选择

| 选项 | 默认值 | 构建 Target | 前置条件 |
| --- | :---: | --- | --- |
| Core，无选项 | ON | `riwo.core` | 选定的 Asio provider |
| `RIWO_BUILD_CORO` | ON | `riwo.coro` | Core |
| `RIWO_BUILD_HTTP` | OFF | `riwo.http` | Coroutines |
| `RIWO_BUILD_WEBSOCKET` | OFF | `riwo.websocket` | HTTP |
| `RIWO_BUILD_UTILITIES` | OFF | `riwo.utils` | Coroutines 与 spdlog |
| `RIWO_BUILD_EXAMPLES` | OFF | `riwo.example.*` | 对应的已启用模块 |

启用模块但关闭其前置模块时，CMake 会拒绝配置。应用应链接每个直接使用的模块；
该模块的下层 Target 会作为公共依赖自动传递。

## 能力开关

| 选项 | 默认值 | 范围 | 外部要求 |
| --- | :---: | --- | --- |
| `RIWO_BUILD_STATIC` | OFF* | 将全部 Riwo 模块构建为静态库 | — |
| `RIWO_ADD_LIBRARY_VERSION` | ON | 为共享库添加版本/SOVERSION | — |
| `RIWO_OPENSSL_SUPPORT` | OFF | TLS、HTTPS 与 WSS | OpenSSL |
| `RIWO_HTTP_ZLIB_SUPPORT` | OFF | HTTP gzip | zlib |
| `RIWO_WEBSOCKET_ZLIB_SUPPORT` | OFF | RFC 7692 `permessage-deflate` | zlib |
| `RIWO_IO_URING_SUPPORT` | OFF | 使用 Asio io_uring 取代 epoll | Linux 与 liburing |
| `RIWO_BUILD_UTILITIES_SBUS_UDP` | ON | UDP 多播软总线传输 | Utilities 模块 |
| `RIWO_UTILS_SBUS_DEFAULT_INTERFACE` | `local` | 未限定软总线 API 的默认实现：`local` 或 `udp` | 设为 `udp` 时需启用 UDP 传输 |

同时启用 HTTP gzip 与 WebSocket 时，WebSocket 压缩也会启用；否则由
`RIWO_WEBSOCKET_ZLIB_SUPPORT` 独立控制 WebSocket 压缩。

*Windows GNU 工具链找不到共享 `libstdc++-6.dll` 时，默认改为静态构建；
此时显式请求共享构建会失败。*

可选的包位置提示：

| 变量 | 包 |
| --- | --- |
| `RIWO_OPENSSL_INSTALL_PREFIX` | OpenSSL 安装前缀 |
| `RIWO_ZLIB_INSTALL_PREFIX` | zlib 安装前缀 |

即使编译了 OpenSSL 支持，TLS 证书加载、对端验证和信任策略仍由应用负责。

## 依赖 provider

| 选项 | 默认值 | 取值/作用 |
| --- | :---: | --- |
| `RIWO_ASIO_PROVIDER` | 空 = `BUNDLED` | `BUNDLED`、`EXTERNAL` 或 `BOOST`；不区分大小写 |
| `RIWO_ASIO_INSTALL_PREFIX` | 空 | 外部 standalone Asio 的位置提示 |
| `RIWO_BOOST_INSTALL_PREFIX` | 空 | Boost.Asio 的位置提示 |
| `RIWO_USE_BUNDLED_SPDLOG` | ON | 使用随附 spdlog 头文件 |
| `RIWO_SPDLOG_INSTALL_PREFIX` | 空 | 外部 spdlog 的位置提示 |

外部 standalone Asio 和 spdlog 可以是 CMake package，也可以是指定前缀下的头文件树：

```sh
cmake -S . -B build \
  -DRIWO_ASIO_PROVIDER=EXTERNAL \
  -DRIWO_ASIO_INSTALL_PREFIX=/path/to/asio \
  -DRIWO_USE_BUNDLED_SPDLOG=OFF \
  -DRIWO_SPDLOG_INSTALL_PREFIX=/path/to/spdlog
```

显式选择 Boost.Asio：

```sh
cmake -S . -B build \
  -DRIWO_ASIO_PROVIDER=BOOST \
  -DRIWO_BOOST_INSTALL_PREFIX=/path/to/boost
```

Riwo 会把选定的 Asio 实现编译进 Core。消费端应使用 Riwo 模块 Target，不要直接
链接 provider Target。

## 工具链与构建资源控制

| 选项 | 默认值 | 约束/作用 |
| --- | :---: | --- |
| `RIWO_USE_LIBCXX` | OFF | 仅 Clang；使用 libc++ 编译和链接 |
| `RIWO_USE_LLD` | OFF | 仅 Clang；使用 lld 链接 |
| `RIWO_ENABLE_LTO` | OFF | 仅 GCC；启用 LTO |
| `RIWO_HEAVY_COMPILE_JOBS` | 随编译器变化 | 限制 HTTP/WebSocket 示例和测试的并行编译数；`0` 表示不限 |
| `RIWO_LOW_MEMORY_DEBUG_INFO` | OFF | 仅 GCC Debug 构建；使用 `-g1` |

`RIWO_HEAVY_COMPILE_JOBS` 在 Clang 下默认为 8、GCC 下为 6，其他编译器下为 0。
Ninja 使用编译 job pool；其他生成器只把已识别的重型 Target 串入对应数量的通道。

## 产物与安装

单配置生成器的构建树：

| 产物 | 路径 |
| --- | --- |
| 共享库与可执行文件 | `<build>/output/bin` |
| 静态库与导入库 | `<build>/output/lib` |
| 示例 | `<build>/output/examples/<module>` |
| 生成的配置头文件 | `<build>/output/config_include` |
| 启用 Fuzz 时的可执行文件 | `<build>/output/fuzz` |

多配置生成器可能插入配置目录。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/riwo-install
cmake --build build --parallel
cmake --install build
```

Visual Studio 等多配置生成器需要在构建和安装命令中添加 `--config Release`。
`RIWO_INSTALL_CMAKEDIR` 可修改生成的 CMake package 文件的安装位置。

## 使用源码树

在 `add_subdirectory()` 前设置 Riwo 选项，然后链接应用直接使用的模块 Target：

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_app LANGUAGES CXX)

set(RIWO_BUILD_HTTP ON CACHE BOOL "")
add_subdirectory(path/to/riwo)

add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_link_libraries(my_app PRIVATE riwo.http)
```

如果同一应用还直接使用 Utilities，则在链接行添加 `riwo.utils`；HTTP 不会隐含
Utilities。

## 使用安装包

安装结果会导出 `Riwo` config package。请求应用使用的模块，并链接带命名空间的
Target：

```cmake
find_package(Riwo 0.16 CONFIG REQUIRED COMPONENTS http)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Riwo::http)
```

安装前缀不能被自动发现时，为消费项目传入
`-DCMAKE_PREFIX_PATH=/path/to/riwo-install`。

| Component | 导入 Target |
| --- | --- |
| `core` | `Riwo::core` |
| `coro` | `Riwo::coro` |
| `http` | `Riwo::http` |
| `websocket` | `Riwo::websocket` |
| `utils` | `Riwo::utils` |

只有构建进安装结果的 component 才可用。package 会恢复选定的 Asio provider，
以及所需的 OpenSSL、zlib、liburing 或外部 spdlog 依赖。它也暴露
`riwo.<module>` 兼容 Target，但新的安装树消费端应优先使用 `Riwo::<module>`。

## 配置约束

CMake 会尽早拒绝不支持的组合，包括：

- 关闭 Coroutines 却启用 HTTP、关闭 HTTP 却启用 WebSocket，或关闭
  Coroutines 却启用 Utilities；
- 禁用 UDP 传输却把它设为默认软总线接口；
- 在非 Linux 平台启用 io_uring，或系统缺少 liburing；
- 在错误的编译器上启用 libc++、lld 或 LTO 专用选项；
- MinGW 缺少共享 GNU C++ 运行时时仍请求共享库；
- [测试指南](../../test/README.md)明确拒绝的测试、Sanitizer、Fuzz、Stress 或
  Performance 组合。

Riwo 是编译型静态/共享库，不是 header-only 库。HTTP 只覆盖 1.0/1.1，
WebSocket 是 HTTP/1.1 上的 RFC 6455。公共 API 尚未达到 1.0，版本间可能变化。
