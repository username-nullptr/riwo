# WebSocket

Language: English | [简体中文](../zh_CN/websocket.md)

| Property | Value |
| --- | --- |
| Build option | `RIWO_BUILD_WEBSOCKET=ON` (default OFF) |
| Source-tree target | `riwo.websocket` |
| Installed target | `Riwo::websocket` |
| Umbrella header | `<riwo/websocket.h>` |
| Depends on | HTTP |
| Optional capabilities | WSS with `RIWO_OPENSSL_SUPPORT`; `permessage-deflate` with WebSocket zlib support |

The module implements RFC 6455 over HTTP/1.1: opening handshakes, clients,
servers, upgraded streams, frame/message I/O, close handling, and explicit
opening retries.

## Header map

| Area | Preferred header | Main facilities |
| --- | --- | --- |
| Client | `<riwo/websocket/client.h>` | WS/WSS opening, redirects, cookies, proxies, diagnostics |
| Server/upgrade | `<riwo/websocket/server.h>` | Owned listener and upgrade from an HTTP service |
| Stream | `<riwo/websocket/stream.h>` | Message/frame I/O, control frames, close and lifecycle |
| Retry | `<riwo/websocket/retry.h>` | Policy-driven retry of failed opening attempts |
| Protocol | `<riwo/websocket/protocol/...>` | Handshake and frame codecs |
| Configuration | `<riwo/websocket/types.h>` | Messages, closes, stream limits, keepalive, compression |

The umbrella header includes the high-level client, server, and retry entry
points. Include stream, types, or protocol headers directly for those lower
levels.

## Open a connection

`websocket::client` owns an HTTP client. `client.open()` opens a connection
through that owned client. The free `websocket::open(http_client, ...)` reuses
an application-owned HTTP client, including its connector, cookies, connection
pool, and proxy policy.

`connect_request` configures request headers/authentication, proxy selection,
stream limits, handshake timeout, redirects, subprotocols, and extensions. The
default proxy policy checks `ws_proxy`/`wss_proxy`, then the matching
HTTP/HTTPS variables, then `all_proxy`, while honoring `no_proxy`.

One `open()` call performs one opening attempt. `retry_open()` can create new
attempts with exponential backoff, a cap, jitter, an attempt limit, a decision
callback, and an observer. It does not monitor an established stream, replay
messages, restore authentication/subscriptions, or run the application receive
loop.

## Accept or upgrade

`websocket::server` owns an HTTP listener and performs opening handshakes.
Choose one delivery model per server:

- call `accept()` explicitly; or
- register `on_connection()`/`on_default()` handlers.

For a mixed HTTP/WebSocket service, route with `http::server`, verify
`is_upgrade_request()`, then call `websocket::upgrade()`. A successful
upgrade returns the stream, an immutable request snapshot, and selected
subprotocol/extension information.

`upgrade_options` controls stream limits, handshake timeout, response headers,
protocol/extension allowlists, and synchronous or awaitable validators and
selectors. A synchronous `upgrade()` cannot run awaitable callbacks.

## Use a stream

| Operation | Result |
| --- | --- |
| `read<Buffer>()` | One complete text or binary message |
| `consume()` | One message delivered in temporary chunks |
| `read_frame<Buffer>()` | One data frame with frame boundaries preserved |
| `write_text()`, `write_binary()`, `write()` | Queue one complete message |
| `write_frame()` | Write an explicitly fragmented data frame |
| `ping()`, `pong()` | Send manual control frames |
| `close()` | Perform the RFC close handshake |
| `shutdown()` | Close the transport immediately |
| `wait_written()` | Observe accepted queued writes |
| `wait_closed()`, `on_closed()` | Observe terminal close information |

For one stream:

- only one of `read()`, `consume()`, or `read_frame()` may be active;
- one read and one write may overlap;
- message writes are serialized through operation- and byte-bounded queues;
- `message_chunk::body` is valid only during its `consume()` callback;
- control callbacks run while an active read processes control frames;
- complete-message reads assemble continuation frames and enforce message
  limits;
- all access must stay on one strand when multiple threads run the event loop.

`stream_config` controls frame/message limits, buffers, fragmentation, queued
writes, close timeout, keepalive, and compression. A positive `ping_interval`
enables automatic Ping/Pong; an active read is required to process Pong frames.
Manual `ping()`/`pong()` is available only when that interval is zero.

## Compression and boundaries

Both peers must negotiate `permessage-deflate`.
`permessage_deflate_options` controls negotiation policy and
`compression_config` controls automatic message compression and thresholds.

The module covers masking, fragmentation, UTF-8 validation, control frames,
WS/WSS, redirects, cookies, subprotocols, HTTP/SOCKS5 proxies, timeouts,
cancellation, and bounded writes. It does not provide WebSocket over HTTP/2 or
HTTP/3, extensions other than `permessage-deflate`, persistent replay, or
application routing.

The shared lifetime and concurrency contract is in
[Execution and I/O model](io-model.md). Runnable clients, servers, mixed
upgrades, retries, proxies, codecs, and WSS are indexed in
[WebSocket examples](../../examples/README.md#websocket).
