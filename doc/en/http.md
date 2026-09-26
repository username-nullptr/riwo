# HTTP

Language: English | [简体中文](../zh_CN/http.md)

| Property | Value |
| --- | --- |
| Build option | `RIWO_BUILD_HTTP=ON` (default OFF) |
| Source-tree target | `riwo.http` |
| Installed target | `Riwo::http` |
| Umbrella header | `<riwo/http.h>` |
| Depends on | Coroutines |
| Optional capabilities | HTTPS with `RIWO_OPENSSL_SUPPORT`; gzip with `RIWO_HTTP_ZLIB_SUPPORT` |

The module implements HTTP/1.0 and HTTP/1.1 client, server, and protocol
building blocks. It does not implement HTTP/2 or HTTP/3.

## Header map

| Area | Preferred headers | Main facilities |
| --- | --- | --- |
| Client | `<riwo/http/client.h>` | Requests, replies, cookies, pooling, connectors, proxies |
| Server | `<riwo/http/server.h>` | Listener, routing, request/response, middleware, sessions |
| Protocol | `<riwo/http/protocol/...>` | Values, headers, cookies, parsers, generators, ranges, forms |
| Transport | `<riwo/http/utils/...>` | TCP/TLS connections and I/O option tokens |

The umbrella header includes the client and server entry points. Include
protocol or transport headers directly when using those lower-level facilities.

## Client lifecycle

A high-level request proceeds through three explicit phases:

1. Start a request with `request_get()`, `request_post()`, or another method.
2. Call `wait_reply()` on the returned request context.
3. Read, consume, or save the reply body.

The default token for these high-level operations is synchronous. Pass
`riwo::use_awaitable` at each asynchronous phase for coroutine use, or use a
compatible callback/future token. Fully consume a body before releasing its
context when the pooled connection should remain reusable.

| Configuration | Controls |
| --- | --- |
| `request_arg` | Headers, cookies, chunk attributes, Basic/Bearer auth, proxy auth |
| `client::req_info` | URL, arguments, proxy choice, redirects, decompression |
| `client_config` | Default proxy and `TCP_NODELAY` |
| `connection_pool_config` | Pool size and connection lifetime |
| `connector` | Direct, TLS, HTTP CONNECT, SOCKS5, or custom connection creation |

The global proxy policy reads lowercase and uppercase
`http_proxy`/`https_proxy`/`all_proxy` and honors `no_proxy`. A request can
inherit that policy, bypass it, or specify an explicit forward/tunnel proxy.

Body transfer helpers report body bytes. They do not include headers, chunk
framing, or multipart boundaries in that count.

## Server lifecycle

`http::server` owns an acceptor and routes requests by method and path. Route
patterns support literal text, `*`, `?`, and named `{arguments}`.

| Area | Main API |
| --- | --- |
| Routing | `on_request()`, `on_default()` |
| Error handling | `on_server_error()`, `on_service_error()` |
| Middleware | `basic_aop`, `basic_ctrlr_aop` |
| Request | Method/version/target, headers, cookies, arguments, body and file reads |
| Response | Status, headers, cookies, fixed/chunked writes, redirects, files, ranges |
| Sessions | `service_context::session()`, `session_or()`, session manager |

`server_config::resource_root` resolves relative paths used by file APIs. It
is a convenience root, not a security sandbox. Validate and constrain every
path derived from request data.

For protocol upgrades, `service_context::hand_over_connection()` transfers the
connection and pending bytes out of HTTP handling. WebSocket services should
normally use [`websocket::upgrade()`](websocket.md), which validates and
completes the RFC 6455 handshake.

## Operation contract

For one connection, at most one read and one write may be active; one of each
may overlap. Serialize request/reply phase changes and access to each client,
server, request context, or reply object. With multiple event-loop threads,
initiate all work for one service from one strand.

Non-detached asynchronous writes borrow their buffers until completion.
Overloads documented as detached/owning keep a copy. Cancellation does not end
that lifetime before the final completion.

HTTPS uses an application-configured `asio::ssl::context`. Riwo supplies the
TLS transport integration but does not choose certificates, verification, or
trust policy.

The complete shared contract is in [Execution and I/O model](io-model.md).
Runnable clients, servers, proxies, files, sessions, codecs, and HTTPS are
indexed in [HTTP examples](../../examples/README.md#http).
