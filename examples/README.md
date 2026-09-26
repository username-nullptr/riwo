# Riwo Examples

This is the canonical catalogue of runnable examples. Programs follow the source
module layout and intentionally stay small enough to read beside the
corresponding guide.

## Build and locate examples

Enable examples and every module you want to inspect:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DRIWO_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

CMake adds examples only for enabled modules and capabilities. In particular,
HTTPS and WSS examples require `RIWO_OPENSSL_SUPPORT=ON`.

| Item | Convention |
| --- | --- |
| CMake target | `riwo.example.<module>.<name>` |
| Single-config executable | `<build>/output/examples/<module>/<name>` |
| Multi-config executable | The generator may add a configuration directory |
| Install destination | `examples/<module>/` below the install prefix |

Build one program without building every example:

```sh
cmake --build build --target riwo.example.core.execution
```

## Core

Core examples have no optional module requirement.

| Program | Demonstrates | Arguments / side effects |
| --- | --- | --- |
| [`algorithms`](core/algorithms.cpp) | MIME, SHA-1, UUID, wildcard matching | None |
| [`app_paths`](core/app_paths.cpp) | Executable, working, home, and absolute paths | None |
| [`args_parser`](core/args_parser.cpp) | Options, flags, help/version, positional values | `-o/--output`, `-v/--verbose`, `--version`, `-h/--help` |
| [`dynamic_library`](core/dynamic_library.cpp) | Load a shared library and resolve a symbol | Optional plugin path; otherwise uses its companion fixture |
| [`execution`](core/execution.cpp) | Dispatch, post, timers, cancellation, event loop | None; runs for about one second |
| [`ini`](core/ini.cpp) | Load, update, and save INI | Optional path; otherwise writes `riwo-example.ini` |
| [`lock_free_queue`](core/lock_free_queue.cpp) | Concurrent producer/consumer queue | None |
| [`value`](core/value.cpp) | Text/numeric conversion and formatting | None |

`dynamic_library_plugin` is a shared-library fixture built beside
`dynamic_library`, not a standalone example.

## Coroutines

Requires `RIWO_BUILD_CORO=ON` (the default).

| Program | Demonstrates |
| --- | --- |
| [`basics`](coro/basics.cpp) | Awaitable start, future wait, delay, worker switch |
| [`mutex`](coro/mutex.cpp) | Coroutine mutex and unique lock |
| [`shared_mutex`](coro/shared_mutex.cpp) | Shared readers and exclusive writer |
| [`semaphore`](coro/semaphore.cpp) | Limited concurrent coroutine work |
| [`condition_variable`](coro/condition_variable.cpp) | Predicate wait and notification |

These programs are self-contained and take no arguments.

## HTTP

Requires `RIWO_BUILD_HTTP=ON`.

| Program | Demonstrates | Arguments / default |
| --- | --- | --- |
| [`protocol`](http/protocol.cpp) | Offline parser and generator | None |
| [`client_sync`](http/client_sync.cpp) | Synchronous request/reply/body | `[url]`; local server on 8080 |
| [`client_awaitable`](http/client_awaitable.cpp) | Coroutine request/reply/body | `[url]`; local server on 8080 |
| [`client_cookies`](http/client_cookies.cpp) | Cookie storage and resend | `[base-url]`; local server on 8080 |
| [`client_file`](http/client_file.cpp) | Upload and download | `<upload-file> [download-file] [base-url]` |
| [`proxy_client`](http/proxy_client.cpp) | HTTP proxy and optional Basic auth | `[target-url] [proxy-url] [user] [password]` |
| [`server`](http/server.cpp) | Routes, path arguments, cookies, errors | `[port]`; 8080 |
| [`server_aop`](http/server_aop.cpp) | Middleware and controller handler | `[port]`; 8081 |
| [`server_file`](http/server_file.cpp) | File response and upload save | `<download-file> [port] [upload-file]`; 8083 |
| [`server_session`](http/server_session.cpp) | Sessions and session cookies | `[port]`; 8082 |
| [`https_server`](http/https_server.cpp) | HTTPS server | `<certificate.pem> <private-key.pem> [port]`; 8443 |

Start a server before its client:

```sh
./build/output/examples/http/server
./build/output/examples/http/client_sync
```

File transfer uses a separate pair:

```sh
./build/output/examples/http/server_file README.md 8083 /tmp/riwo-uploaded.bin
./build/output/examples/http/client_file \
  README.md /tmp/riwo-downloaded.md http://127.0.0.1:8083
```

`https_server` is built only with OpenSSL support. `proxy_client` requires an
external proxy. Paths derived from request data must be validated by the
application; `resource_root` is not a sandbox.

## WebSocket

Requires `RIWO_BUILD_WEBSOCKET=ON`.

| Program | Demonstrates | Arguments / default |
| --- | --- | --- |
| [`protocol`](websocket/protocol.cpp) | Offline handshake and frame codecs | None |
| [`server`](websocket/server.cpp) | Owned echo server | `[port]`; 8080 at `/echo` |
| [`client`](websocket/client.cpp) | Open, message I/O, close | `[endpoint]`; local echo server |
| [`retry_open`](websocket/retry_open.cpp) | Application-controlled opening recovery | `[endpoint]`; local echo server |
| [`proxy_client`](websocket/proxy_client.cpp) | HTTP/SOCKS5 proxy | `[endpoint] [proxy-url] [user] [password]` |
| [`mixed_http_server`](websocket/mixed_http_server.cpp) | HTTP route plus Upgrade | `[port]`; 8080 at `/mixed` |
| [`mixed_http_client`](websocket/mixed_http_client.cpp) | HTTP request followed by Upgrade | `[http-url] [websocket-url]` |
| [`wss_server`](websocket/wss_server.cpp) | TLS echo server | `<certificate.pem> <private-key.pem> [port]`; 8443 |
| [`wss_client`](websocket/wss_client.cpp) | TLS client and trust setup | `[endpoint] [ca-certificate.pem]` |

Run either local pair with the server first:

```sh
./build/output/examples/websocket/server
./build/output/examples/websocket/client

./build/output/examples/websocket/mixed_http_server
./build/output/examples/websocket/mixed_http_client
```

The basic and mixed servers both default to port 8080, so do not run them
together without changing a port. `retry_open` is intentionally long-running.
Proxy examples require an external proxy. WSS programs are built only with
OpenSSL support.

## Utilities

Requires `RIWO_BUILD_UTILITIES=ON`.

| Program | Demonstrates | Arguments / side effects |
| --- | --- | --- |
| [`logger`](utils/logger.cpp) | Default and named loggers | Optional directory; otherwise writes `./logs` |
| [`settings`](utils/settings.cpp) | Settings, signals, persistence | Optional INI path; otherwise writes `riwo-example-settings.ini` |
| [`signal_slot`](utils/signal_slot.cpp) | Function and lambda slots | None |
| [`observer`](utils/observer.cpp) | ID-addressed callback lifecycle | None |
| [`modules`](utils/modules) | Dependency graph and ordered initialization | None |
| [`process`](utils/process.cpp) | Child start, stdout, join, exit code | Runs the platform echo command |
| [`soft_bus_local`](utils/soft_bus_local.cpp) | In-process publish/subscribe/cache | None |
| [`soft_bus_udp`](utils/soft_bus_udp.cpp) | UDP multicast transport | Requires UDP transport and local multicast |
| [`soft_bus_transport`](utils/soft_bus_transport.cpp) | Custom transport boundary | None |

`soft_bus_udp` is added only when `RIWO_BUILD_UTILITIES_SBUS_UDP=ON`. Pass
explicit paths to keep generated files outside the source tree:

```sh
./build/output/examples/utils/logger /tmp/riwo-example-logs
./build/output/examples/utils/settings /tmp/riwo-example-settings.ini
```
