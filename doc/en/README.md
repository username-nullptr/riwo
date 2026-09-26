# Riwo Documentation

Language: English | [简体中文](../zh_CN/README.md)

The documentation mirrors the current project: first understand the module
graph and build boundary, then read the shared execution rules, then the module
you use.

## Choose a route

| Goal | Start here | Continue with |
| --- | --- | --- |
| Evaluate the library | [Project structure](architecture.md) | A module guide below |
| Build or integrate it | [Build and configuration](build.md) | [Examples](../../examples/README.md) |
| Write asynchronous code | [Execution and I/O model](io-model.md) | [Coroutines](coroutines.md) |
| Use network protocols | [HTTP](http.md) or [WebSocket](websocket.md) | [Execution and I/O model](io-model.md) |
| Develop or verify Riwo | [Project structure](architecture.md) | [Tests](../../test/README.md) |

## Module guides

| Module | Purpose | Guide |
| --- | --- | --- |
| `riwo.core` | Common data types, execution, algorithms, system and synchronization facilities | [Core](core.md) |
| `riwo.coro` | Coroutine waits and synchronization | [Coroutines](coroutines.md) |
| `riwo.http` | HTTP/1.0 and HTTP/1.1 client, server, and protocol tools | [HTTP](http.md) |
| `riwo.websocket` | RFC 6455 clients, servers, streams, and codecs | [WebSocket](websocket.md) |
| `riwo.utils` | Logging, settings, signals, processes, modules, and soft bus | [Utilities](utilities.md) |

## Documentation boundaries

- Public declarations in [`riwo/`](../../riwo) are the API authority.
- [Build and configuration](build.md) is the authority for CMake switches,
  output paths, installation, and consumer targets.
- [Execution and I/O model](io-model.md) owns the shared executor, lifetime,
  cancellation, and concurrency rules; module pages document exceptions and
  per-object limits.
- [Examples](../../examples/README.md) owns runnable program names and
  arguments. [Tests](../../test/README.md) owns verification profiles and
  selection commands.
- Files below a `detail/` directory support implementation and public
  templates. Do not treat their names as preferred application entry points.
