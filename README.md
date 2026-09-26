# Riwo

Riwo (རི་བོ་) is a modular C++20 library built around Asio. It provides a shared
execution layer, coroutine synchronization, HTTP/1.x, WebSocket, and
application utilities without requiring applications to adopt every module.

## Module map

```text
riwo.core
└── riwo.coro
    ├── riwo.http
    │   └── riwo.websocket
    └── riwo.utils
```

| Module | Build switch | Default | Primary include | Source-tree target |
| --- | --- | :---: | --- | --- |
| Core | always built | ON | `<riwo/core.h>` | `riwo.core` |
| Coroutines | `RIWO_BUILD_CORO` | ON | `<riwo/coro.h>` | `riwo.coro` |
| HTTP | `RIWO_BUILD_HTTP` | OFF | `<riwo/http.h>` | `riwo.http` |
| WebSocket | `RIWO_BUILD_WEBSOCKET` | OFF | `<riwo/websocket.h>` | `riwo.websocket` |
| Utilities | `RIWO_BUILD_UTILITIES` | OFF | `<riwo/utils.h>` | `riwo.utils` |

Link each module your application uses directly. CMake propagates that
module's lower-level dependencies. `<riwo.h>` includes the umbrella header for
every module enabled when Riwo was configured.

## Build

Requirements: CMake 3.15+, C++20, and GCC 13+, Clang 17+, or MSVC 19.30+
(Visual Studio 2022+).

The default build contains Core and Coroutines:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Enable optional modules explicitly. The following profile builds the complete
library and all applicable examples:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DRIWO_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

See [Build and configuration](doc/en/build.md) for feature switches,
dependency providers, installation, and CMake consumption.

## Documentation

| Topic | English | 简体中文 |
| --- | --- | --- |
| Documentation index | [Open](doc/en/README.md) | [打开](doc/zh_CN/README.md) |
| Project structure | [Open](doc/en/architecture.md) | [打开](doc/zh_CN/architecture.md) |
| Build and integration | [Open](doc/en/build.md) | [打开](doc/zh_CN/build.md) |
| Execution and I/O model | [Open](doc/en/io-model.md) | [打开](doc/zh_CN/io-model.md) |

Runnable programs are catalogued in [Examples](examples/README.md). Verification
profiles and test selection are documented in [Tests](test/README.md).

## Repository layout

| Path | Role |
| --- | --- |
| [`riwo/`](riwo) and [`riwo.h`](riwo.h) | Library headers and implementations, arranged by module |
| [`cmake/`](cmake) | Build policies, feature selection, and package export |
| [`3rd_party/`](3rd_party/SOURCE.txt) | Bundled Asio and spdlog providers |
| [`examples/`](examples/README.md) | Small programs grouped by the module they exercise |
| [`test/`](test/README.md) | Functional, CMake, stress, fuzz, and performance suites |
| [`doc/`](doc/README.md) | English and Simplified Chinese guides |

The headers are the API authority. Guides describe supported entry points,
workflows, ownership, and concurrency constraints; examples show concrete
usage; tests define verified behavior.

## License

Riwo is licensed under the [MIT License](LICENSE.txt). Bundled dependencies
retain their own licenses.
