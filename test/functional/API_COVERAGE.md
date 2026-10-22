# Functional API Coverage

[Test guide](../README.md)

Functional tests own the public API contract. This map identifies where each
module's callable surface is exercised; it is intentionally organized by public
behavior rather than by implementation source file.

## Core

- `core/basic.cpp`: algorithms, hashes, UUIDs, byte order, string tools,
  optional/expected values, buffer helpers, spin/shared locks, and type utilities.
- `core/facilities.cpp`: value/string containers, URL parse/resolve, command-line
  parsing, INI memory/file/token APIs, lock-free queue variants, MIME detection,
  and application paths/environment.
- `core/runtime.cpp`: global/local execution, dispatch/post/delay, timers,
  cancellation, event pumps, futures, awaitables, public `post_completion`,
  associated immediate executors, and exception recovery.
- `core/jthread.cpp`: the fallback joining-thread and stop-token/source/callback
  API, including ownership, moves, automatic stop/join, callback lifetime, and
  concurrent callback destruction.
- `core/public_api.cpp`: flags, parameter containers, value conversion overloads,
  formatting, endpoint wrappers, environment operations, dynamic libraries, and
  compile-time public type properties.

## Coroutine

- `coro/basic.cpp`: mutex/unique-lock, semaphore, condition variable,
  shared-mutex, timed operations, executor selection, sleep, and future/thread
  waiting APIs, including contention and timeout behavior.

## HTTP

- `http/basic.cpp`: protocol enums/cookies, client URL/proxy configuration,
  connection read/write accounting, file bodies, connection pools, and malformed
  reply recovery.
- `http/protocol.cpp`: incremental request/response parsers, generators, ranges,
  multipart bodies, conditions, upgrades, form data, authentication, and cookie
  storage policy.
- `http/public_api.cpp`: endpoint/value containers, client config, every named
  client request/context convenience method, compression policy/codecs, file
  option tokens, and sessions.
- `http/loopback.cpp`: owned server/client request and response APIs, route
  updates, keep-alive, and static-file cache behavior.
- `interop/http_interop.py`: independent client/server compatibility when a local
  backend is available.

## Experimental HTTP

- `http_exp/public_api.cpp`: public types, completion signatures, configuration,
  client/server construction, and compile-time ownership contracts.
- `http_exp/protocol_api.cpp`: HTTP/1 and HTTP/2 incremental engines, event/action
  flow control, codecs, public V2 diagnostics extensions, and malformed input.
- `http_exp/body.cpp`: body sources/readers/writers, exchange state machines,
  request metadata, informational responses, and reply convenience operations.
- `http_exp/http1.cpp`: HTTP/1.0 and HTTP/1.1 loopback, buffered/file/streaming
  bodies, trailers, slow reads, cancellation, and bodyless status responses.
- `http_exp/session_pool.cpp`: protocol selection, session keys/factories, pool
  capacity/fairness/expiry, non-blocking asynchronous connector start,
  cleartext preface detection, and keep-alive reuse.
- `http_exp/http2_client.cpp`: HTTP/2 prior knowledge, multiplexed streams,
  request/response bodies and trailers, RST_STREAM/GOAWAY mapping, automatic
  retry, redirects, and replayable-source rewind behavior.
- `http_exp/http2_server.cpp`: HTTP/2 prior-knowledge routing, multiplexed
  exchanges, stream extensions, resource validation, HEAD/bodyless responses,
  trailers, and graceful shutdown.
- `http_exp/application.cpp`: public-API application workflow covering routing,
  query targets, authentication rejection and body draining, state mutation,
  case-insensitive headers, streaming events/trailers, and 404 responses.
- `http_exp/regressions.cpp`: raw HTTP/1 wire regressions for target forms,
  OPTIONS/Allow, method rejection, 100-continue, HEAD, and invalid fields.
- `interop/http_interop.py`: independent HTTP_EXP client/server compatibility
  when a local backend is available.

## Experimental WebSocket

- `websocket_exp/public_api.cpp`: umbrella/value types, executor propagation,
  HTTP_EXP duplex-channel ownership, retry policy, invalid open completion, and
  unopened stream errors.
- `websocket_exp/protocol.cpp`: errors, randomness, opcode/close helpers,
  masking, frame generation/parsing, fragmentation, HTTP/1 Upgrade, and HTTP/2
  Extended CONNECT handshakes.
- `websocket_exp/client_server.cpp`: HTTP/1.1 Upgrade and HTTP/2 Extended CONNECT
  round trips, subprotocol and compression negotiation, overlapped read/write,
  clean close, and validator rejection response preservation.

## WebSocket

- `websocket/protocol.cpp`: errors, randomness, opcode/close helpers, masking,
  frame generation/parsing, fragmentation, and opening handshakes.
- `websocket/public_api.cpp`: umbrella types, configuration/value objects,
  executor-bound objects, completion signatures, callbacks, and retry result APIs.
- `websocket/handshake.cpp`: HTTP upgrade, proxies, redirects, validators,
  extension/subprotocol negotiation, and timeout bounds.
- `websocket/client_server.cpp`: owned client/server lifecycle, accept queues,
  cancellation, retry recovery, simultaneous close, and compression negotiation.
- `websocket/stream.cpp`: sync/async reads and writes, frame/message boundaries,
  control callbacks, queue limits, cancellation, automatic ping/pong, compression,
  close deadlines, and lifecycle transitions.
- `websocket/wss.cpp`: TLS verification and secure upgrade when OpenSSL is enabled.
- `interop/websocket_interop.py`: independent implementation compatibility when a
  local backend is available.

## Utilities

- `utils/modules.cpp`: umbrella/module compile and initialization surface.
- `utils/basic.cpp`: signal-slot modes, observer ownership, mutation, blocking,
  argument ownership, synchronous/asynchronous/awaitable delivery.
- `utils/runtime.cpp`: settings persistence and async tokens, process lifecycle,
  pipes/environment/cancellation, local message bus, and logger configuration/I/O.
- `utils/sbus.cpp`: raw/typed publish-subscribe, cancellation, topic indexing,
  large-payload fanout ownership, UDP scope/fragmentation, and receive statistics.
- `utils/sbus_cache.cpp`: cache set/get overloads, snapshots, typed topics,
  duplicate suppression, local publish updates, change signals, and
  edge-triggered `wait_changed` across synchronous, callback, future, deferred,
  and awaitable tokens, including timeout, token cancellation, and cache cancel.

When a public API changes, update its successful, invalid-input, ownership,
cancellation, timeout, and state-transition coverage here. Use Stress, Fuzz, or
Performance only when the behavior matches those suite definitions in the
[test guide](../README.md).
