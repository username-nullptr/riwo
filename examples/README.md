# Riwo Examples

This is the canonical catalogue of runnable examples. Programs follow the source
module layout and intentionally stay small enough to read beside the
corresponding guide.

## Build and locate examples

Enable examples and every module you want to inspect:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_HTTP_EXP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_WEBSOCKET_EXP=ON \
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

## Experimental HTTP

Requires `RIWO_BUILD_HTTP_EXP=ON`. Like the stable HTTP examples, each program
focuses on one capability. The first programs show the everyday client/server
API; later programs cover streaming, files, protocol engines, and custom
transports. The same high-level API works with HTTP/1.x and, when
`RIWO_HTTP_EXP_HTTP2_SUPPORT=1`, HTTP/2.

| Program | Demonstrates | Arguments / default |
| --- | --- | --- |
| [`client_sync`](http_exp/client_sync.cpp) | Minimal synchronous GET with explicit error-code handling | `[url]`; local server |
| [`client_awaitable`](http_exp/client_awaitable.cpp) | Minimal coroutine GET using `use_awaitable` | `[url]`; local server |
| [`client_cookies`](http_exp/client_cookies.cpp) | Cookie capture and resend while following a redirect | `[base-url]`; local server |
| [`server`](http_exp/server.cpp) | Routing, path/query parameters, cookies, redirects, buffered and streaming bodies, trailers, and optional gzip | `[port] [auto\|http1\|http2]`; 8080, auto |
| [`client`](http_exp/client.cpp) | Runtime client configuration, buffered upload, streamed download, trailers, optional decompression, and selected protocol | `[base-url] [auto\|http1\|http2]`; local server, auto |
| [`server_aop`](http_exp/server_aop.cpp) | Route middleware, controller objects, and an explicit executor | `[port]`; 8081 |
| [`server_session`](http_exp/server_session.cpp) | Application sessions, attributes, visit count, and session cookie | `[port]`; 8082 |
| [`client_file`](http_exp/client_file.cpp) | File upload/download, progress, conditional GET, Range GET | `<upload-file> [download-file] [base-url] [auto\|http1\|http2]`; local server |
| [`proxy_client`](http_exp/proxy_client.cpp) | HTTP/SOCKS5 proxy, CONNECT for TLS, and optional Basic auth | `[target-url] [proxy-url] [user] [password]` |
| [`server_file`](http_exp/server_file.cpp) | File response, ETag and Range handling, streamed upload to disk | `<download-file> [port] [upload-file] [auto\|http1\|http2]`; 8083 |
| [`https_server`](http_exp/https_server.cpp) | TLS acceptor with ALPN-based HTTP/1.1 or HTTP/2 selection | `<certificate.pem> <private-key.pem> [port] [auto\|http1\|http2]`; 8443 |
| [`server_acceptor`](http_exp/server_acceptor.cpp) | User-defined acceptor injected into the high-level server | `[port]`; 8086 |
| [`transport`](http_exp/transport.cpp) | User-defined asynchronous byte stream and connector injected into the high-level client | none; no socket is opened |
| [`protocol`](http_exp/protocol.cpp) | Socket-free request/response loop over the public HTTP/1 and optional HTTP/2 engines | none |

Run the pair in separate terminals:

```sh
./build/output/examples/http_exp/server 8080 auto
./build/output/examples/http_exp/client http://127.0.0.1:8080 auto
```

The focused client examples use the same server:

```sh
./build/output/examples/http_exp/client_sync
./build/output/examples/http_exp/client_awaitable
./build/output/examples/http_exp/client_cookies
```

Middleware and session behavior run as small, isolated servers:

```sh
./build/output/examples/http_exp/server_aop 8081
./build/output/examples/http_exp/server_session 8082
```

For cleartext HTTP/2, select it explicitly on both sides:

```sh
./build/output/examples/http_exp/server 8080 http2
./build/output/examples/http_exp/client http://127.0.0.1:8080 http2
```

Exercise the file-specific API in a separate pair. The server exposes the first
file for download and saves uploads to the third argument:

```sh
./build/output/examples/http_exp/server_file README.md 8083 \
  /tmp/riwo-http-exp-uploaded.md auto
./build/output/examples/http_exp/client_file README.md \
  /tmp/riwo-http-exp-downloaded.md http://127.0.0.1:8083 auto
```

`client_file` also performs an `If-None-Match` request and a `Range` request,
so the output demonstrates `304` and `206` handling in addition to full file
transfer. Run `transport` or `protocol` without a server. `server_acceptor`
uses a real TCP socket but replaces the server's default acceptor adapter.

The file pair serves and writes only command-line configured paths. If you
extend those routes to use request-derived filenames, normalize and constrain
the paths before opening them; `resource_root` is not a sandbox.

`http2` is accepted only when nghttp2 was detected at configure time. `auto`
uses normal scheme policy; for a plain `http://` client URL that means HTTP/1.1.
`https_server` is built only with OpenSSL support. `proxy_client` requires an
external HTTP or SOCKS5 proxy.

## Experimental WebSocket

Requires `RIWO_BUILD_WEBSOCKET_EXP=ON`, which also enables the separate
`riwo.websocket_exp` module's dependency on `riwo.http_exp`. These examples do
not use the stable `riwo.websocket` module.

| Program | Demonstrates | Arguments / default |
| --- | --- | --- |
| [`server`](websocket_exp/server.cpp) | HTTP/1 Upgrade and HTTP/2 Extended CONNECT echo service | `[port] [auto\|http1\|http2]`; 8085, auto |
| [`client`](websocket_exp/client.cpp) | Subprotocol, optional compression, text echo, clean close, selected handshake | `[endpoint] [http1\|http2]`; local server, http1 |

Run the HTTP/1.1 pair:

```sh
./build/output/examples/websocket_exp/server 8085 auto
./build/output/examples/websocket_exp/client ws://127.0.0.1:8085/echo http1
```

Or, when HTTP/2 support is compiled in, use Extended CONNECT:

```sh
./build/output/examples/websocket_exp/server 8085 http2
./build/output/examples/websocket_exp/client ws://127.0.0.1:8085/echo http2
```

WebSocket_EXP and HTTP_EXP have distinct build switches, targets, headers,
namespaces, examples, and functional test suites. WebSocket_EXP reuses only
HTTP_EXP's protocol-neutral duplex channel and HTTP transport/session layer.

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
