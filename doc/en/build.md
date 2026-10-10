# Build and Configuration

Language: English | [简体中文](../zh_CN/build.md)

This page is the CMake reference for the current project. Test-only options and
profiles live in the [test guide](../../test/README.md).

## Requirements

| Component | Minimum |
| --- | --- |
| CMake | 3.15 |
| Language mode | C++20 |
| GCC | 13 |
| Clang | 17 |
| MSVC | 19.30 (Visual Studio 2022) |

The repository bundles standalone Asio and spdlog. OpenSSL, zlib, and liburing
are discovered only when the corresponding capability is enabled.

## Choose a build profile

Default library: Core plus Coroutines.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

All library modules and all applicable examples:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DRIWO_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

TLS and compression are separate capabilities. For example:

```sh
cmake -S . -B build-secure -DCMAKE_BUILD_TYPE=Release \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_OPENSSL_SUPPORT=ON \
  -DRIWO_HTTP_ZLIB_SUPPORT=ON
cmake --build build-secure --parallel
```

## Module selection

| Option | Default | Built target | Requires |
| --- | :---: | --- | --- |
| Core, no option | ON | `riwo.core` | selected Asio provider |
| `RIWO_BUILD_CORO` | ON | `riwo.coro` | Core |
| `RIWO_BUILD_HTTP` | OFF | `riwo.http` | Coroutines |
| `RIWO_BUILD_WEBSOCKET` | OFF | `riwo.websocket` | HTTP |
| `RIWO_BUILD_UTILITIES` | OFF | `riwo.utils` | Coroutines and spdlog |
| `RIWO_BUILD_EXAMPLES` | OFF | `riwo.example.*` | corresponding enabled modules |

CMake rejects an enabled module whose prerequisite is disabled. Link every
module used directly; its lower-level targets are public transitive
dependencies.

## Capabilities

| Option | Default | Scope | External requirement |
| --- | :---: | --- | --- |
| `RIWO_BUILD_STATIC` | OFF* | Build all Riwo modules as static libraries | — |
| `RIWO_ADD_LIBRARY_VERSION` | ON | Add version/SOVERSION to shared libraries | — |
| `RIWO_OPENSSL_SUPPORT` | OFF | TLS, HTTPS, and WSS | OpenSSL |
| `RIWO_HTTP_ZLIB_SUPPORT` | OFF | HTTP gzip | zlib |
| `RIWO_WEBSOCKET_ZLIB_SUPPORT` | OFF | RFC 7692 `permessage-deflate` | zlib |
| `RIWO_IO_URING_SUPPORT` | OFF | Enable Asio io_uring file I/O while retaining epoll as the portable reactor | Linux and liburing |
| `RIWO_BUILD_UTILITIES_SBUS_UDP` | ON | UDP multicast soft-bus transport | Utilities module |
| `RIWO_UTILS_SBUS_DEFAULT_INTERFACE` | `local` | Default unqualified soft-bus API; `local` or `udp` | UDP transport if set to `udp` |

When HTTP gzip and WebSocket are both enabled, WebSocket compression is enabled
as well. Otherwise `RIWO_WEBSOCKET_ZLIB_SUPPORT` controls WebSocket compression
independently.

With `RIWO_IO_URING_SUPPORT` enabled, Asio uses io_uring for regular-file
operations and epoll for the platform reactor. Ordinary sockets, timers,
signals, and the default event loop therefore remain available when the
io_uring setup syscall is denied by a container, seccomp profile, or LSM.
Applications that directly create Asio file objects must handle an unavailable
io_uring service.

*On Windows with a GNU toolchain, the default becomes static when CMake cannot
find a shared `libstdc++-6.dll`. Explicitly requesting a shared build in that
case is rejected.*

Optional package hints:

| Variable | Package |
| --- | --- |
| `RIWO_OPENSSL_INSTALL_PREFIX` | OpenSSL installation prefix |
| `RIWO_ZLIB_INSTALL_PREFIX` | zlib installation prefix |

TLS certificate loading, peer verification, and trust policy remain application
responsibilities even when OpenSSL support is compiled in.

## Dependency providers

| Option | Default | Values/effect |
| --- | :---: | --- |
| `RIWO_ASIO_PROVIDER` | empty = `BUNDLED` | `BUNDLED`, `EXTERNAL`, or `BOOST`; case-insensitive |
| `RIWO_ASIO_INSTALL_PREFIX` | empty | Hint for external standalone Asio |
| `RIWO_BOOST_INSTALL_PREFIX` | empty | Hint for Boost.Asio |
| `RIWO_USE_BUNDLED_SPDLOG` | ON | Use the bundled spdlog headers |
| `RIWO_SPDLOG_INSTALL_PREFIX` | empty | Hint for external spdlog |

External standalone Asio and spdlog can be CMake packages or header trees below
the supplied prefixes:

```sh
cmake -S . -B build \
  -DRIWO_ASIO_PROVIDER=EXTERNAL \
  -DRIWO_ASIO_INSTALL_PREFIX=/path/to/asio \
  -DRIWO_USE_BUNDLED_SPDLOG=OFF \
  -DRIWO_SPDLOG_INSTALL_PREFIX=/path/to/spdlog
```

Select Boost.Asio explicitly:

```sh
cmake -S . -B build \
  -DRIWO_ASIO_PROVIDER=BOOST \
  -DRIWO_BOOST_INSTALL_PREFIX=/path/to/boost
```

Riwo compiles the selected Asio implementation into Core. Consumers should use
Riwo module targets, not provider targets.

## Toolchain and build-resource controls

| Option | Default | Constraint/effect |
| --- | :---: | --- |
| `RIWO_USE_LIBCXX` | OFF | Clang only; compile and link with libc++ |
| `RIWO_USE_LLD` | OFF | Clang only; link with lld |
| `RIWO_ENABLE_LTO` | OFF | GCC only; enable LTO |
| `RIWO_HEAVY_COMPILE_JOBS` | compiler-dependent | Bound concurrent HTTP/WebSocket example and test compilation; `0` disables |
| `RIWO_LOW_MEMORY_DEBUG_INFO` | OFF | GCC Debug builds only; use `-g1` |

`RIWO_HEAVY_COMPILE_JOBS` defaults to 8 for Clang, 6 for GCC, and 0 for other
compilers. Ninja uses a compile job pool; other generators serialize only the
identified heavy targets into that many lanes.

## Outputs and installation

For a single-config generator:

| Artifact | Build-tree path |
| --- | --- |
| Shared libraries and executables | `<build>/output/bin` |
| Static and import libraries | `<build>/output/lib` |
| Examples | `<build>/output/examples/<module>` |
| Generated configuration headers | `<build>/output/config_include` |
| Fuzz executables, when enabled | `<build>/output/fuzz` |

Multi-config generators may insert a configuration directory.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/riwo-install
cmake --build build --parallel
cmake --install build
```

Use `--config Release` on both build and install commands for Visual Studio and
other multi-config generators. `RIWO_INSTALL_CMAKEDIR` changes the destination
for the generated CMake package files.

## Consume the source tree

Set Riwo options before `add_subdirectory()`, then link the module targets used
by the application:

```cmake
cmake_minimum_required(VERSION 3.15)
project(my_app LANGUAGES CXX)

set(RIWO_BUILD_HTTP ON CACHE BOOL "")
add_subdirectory(path/to/riwo)

add_executable(my_app main.cpp)
target_compile_features(my_app PRIVATE cxx_std_20)
target_link_libraries(my_app PRIVATE riwo.http)
```

If the same application also uses Utilities directly, add `riwo.utils` to the
link line; HTTP does not imply Utilities.

## Consume an installation

Installations export the `Riwo` config package. Request the modules the
application uses and link the namespaced targets:

```cmake
find_package(Riwo 0.16 CONFIG REQUIRED COMPONENTS http)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Riwo::http)
```

Configure the consumer with
`-DCMAKE_PREFIX_PATH=/path/to/riwo-install` when the prefix is not otherwise
discoverable.

| Component | Imported target |
| --- | --- |
| `core` | `Riwo::core` |
| `coro` | `Riwo::coro` |
| `http` | `Riwo::http` |
| `websocket` | `Riwo::websocket` |
| `utils` | `Riwo::utils` |

A component is available only if it was built into the installation. The
package restores the selected Asio provider and any required OpenSSL, zlib,
liburing, or external spdlog dependency. It also exposes `riwo.<module>`
compatibility targets, but new install-tree consumers should prefer
`Riwo::<module>`.

## Configuration constraints

CMake fails early for unsupported combinations, including:

- HTTP without Coroutines, WebSocket without HTTP, or Utilities without
  Coroutines;
- UDP as the default soft-bus interface while its transport is disabled;
- io_uring on a non-Linux platform or without liburing;
- compiler-specific libc++, lld, or LTO options on the wrong compiler;
- shared MinGW libraries without a shared GNU C++ runtime;
- test, sanitizer, fuzz, stress, or performance combinations rejected by the
  [test guide](../../test/README.md).

Riwo is a compiled static/shared library, not a header-only library. HTTP is
limited to 1.0/1.1; WebSocket is RFC 6455 over HTTP/1.1. Public APIs are pre-1.0
and may change between releases.
