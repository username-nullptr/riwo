# Project Structure

Language: English | [简体中文](../zh_CN/architecture.md)

This page maps the current repository and module boundaries. It describes the
source tree as it exists now; it is not a migration history.

## Layers and dependencies

```text
riwo.core
└── riwo.coro
    ├── riwo.http
    │   └── riwo.websocket
    └── riwo.utils
```

| Layer | Responsibility | May depend on |
| --- | --- | --- |
| Core | Asio adaptation, execution, common values, algorithms, files, system and thread primitives | Selected Asio provider; optional OpenSSL/liburing |
| Coroutines | Suspending locks, condition variables, semaphores, waits, and executor switching | Core |
| HTTP | HTTP/1.x values, codecs, connections, client, server, routing, sessions | Coroutines; optional zlib |
| WebSocket | RFC 6455 handshake, frame codecs, clients, servers, streams, retry | HTTP; optional zlib |
| Utilities | Logging, settings, signals, observers, processes, module startup, soft bus | Coroutines; spdlog |

Dependencies point downward only. HTTP and Utilities are siblings; WebSocket is
the only library module layered on HTTP. Enabling a higher module requires its
lower modules, and CMake publishes those dependencies transitively.

## Source tree

| Path | Contents | Rule |
| --- | --- | --- |
| `riwo.h` | Build-sensitive umbrella header | Includes Core and every enabled optional module |
| `riwo/<module>.h` | Module umbrella headers | Convenient entry points, not exhaustive for specialized APIs |
| `riwo/<module>/` | Public declarations, template support, and implementations | Follow the same boundary as the matching CMake target |
| `cmake/` | Project options, platform policy, target creation, testing, package export | Canonical source for configuration behavior |
| `3rd_party/` | Bundled Asio and spdlog | Provider boundary, not Riwo API |
| `examples/<module>/` | Focused runnable programs | One concept or workflow per executable |
| `test/functional/` | Deterministic API contracts | Default behavioral verification |
| `test/cmake/` | Configuration and install-consumer contracts | Validates the integration boundary |
| `test/stress/` | Concurrency and repeated-lifecycle pressure | Opt-in |
| `test/fuzz/` | Input and operation-sequence exploration | Dedicated Clang/libFuzzer build |
| `test/performance/` | Measurements without fixed pass/fail thresholds | Opt-in Release build |
| `doc/en/`, `doc/zh_CN/` | Conceptual and integration guides | Mirrored by topic and source module |

## Public entry points

Applications normally include the narrowest documented header they need. The
module umbrella headers are useful when compile time and include breadth are not
concerns. `<riwo.h>` is convenient for small applications whose build enables
all required modules.

Source-tree consumers link `riwo.<module>`. Installed-package consumers request
the corresponding component and link `Riwo::<module>`:

| Module | Source tree | Installed package |
| --- | --- | --- |
| Core | `riwo.core` | `Riwo::core` |
| Coroutines | `riwo.coro` | `Riwo::coro` |
| HTTP | `riwo.http` | `Riwo::http` |
| WebSocket | `riwo.websocket` | `Riwo::websocket` |
| Utilities | `riwo.utils` | `Riwo::utils` |

Provider targets such as `riwo.asio` and `riwo.spdlog` exist to carry build
requirements. Applications should link module targets instead of depending on
provider targets directly.

## Cross-cutting contracts

The module split does not replace the common runtime contract:

- executor-aware work follows Asio's executor and completion-token model;
- borrowed buffers and referenced state remain alive until completion;
- access to one stateful I/O object is serialized unless its declaration says
  otherwise;
- optional TLS, compression, and transport capabilities are fixed when Riwo is
  configured and exposed through generated configuration headers.

Read [Execution and I/O model](io-model.md) before sharing I/O objects across
threads. Read [Build and configuration](build.md) before choosing modules,
providers, TLS, compression, or installation boundaries.

## Where changes belong

| Change | Primary location | Documentation/check |
| --- | --- | --- |
| Public API behavior | Matching `riwo/<module>/` area | Module guide and functional coverage |
| Module or feature dependency | CMake file that owns the option | Build guide and CMake tests |
| Runnable usage scenario | `examples/<module>/` | Examples index |
| Deterministic contract | `test/functional/<module>/` | Functional coverage map |
| Pressure, malformed input, or measurement | Stress, fuzz, or performance suite | Test guide |

This keeps each fact owned once while allowing indexes to link to it.
