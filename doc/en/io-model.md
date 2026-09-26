# Execution and I/O Model

Language: English | [简体中文](../zh_CN/io-model.md)

This contract applies across Core, Coroutines, HTTP, WebSocket, and Utilities.
Module guides add object-specific rules; they do not replace this page.

## Select a runtime

Riwo exposes a process-wide default runtime:

| API | Role |
| --- | --- |
| `riwo::io_context()` | Access the default `asio::io_context` |
| `riwo::get_executor()` | Access its executor |
| `riwo::exec()` | Run it until stopped |
| `riwo::exit(code)` | Stop it and set the process result returned by `exec()` |

Executor-aware overloads also accept an application-owned Asio executor. Use
the default runtime for a simple process-wide loop; use owned contexts, strands,
or thread pools when the application needs isolation, explicit thread
placement, or controlled shutdown order.

An executor answers *where work runs*. It does not own every object captured by
that work and does not make an I/O object thread-safe.

## Select a completion style

Asynchronous APIs use Asio completion tokens when their declaration permits it:

| Token/style | Result model |
| --- | --- |
| `riwo::use_sync` or a synchronous default | Complete before the call returns |
| `riwo::use_awaitable` | Return an `riwo::awaitable<T>`; I/O errors become `std::system_error` |
| `riwo::use_future` | Return a future; I/O errors become exceptions |
| `riwo::detached` | No completion value is observed by the caller |
| Callback token | Invoke the supplied completion handler |
| `riwo::deferred` | Return a deferred operation where supported |

Not every operation accepts every token; the function declaration is
authoritative. A token can also carry an associated executor, allocator,
immediate executor, cancellation slot, error redirection, or timeout. Those
properties affect the completion, not the lifetime of borrowed arguments.

Asynchronous initiating functions deliver immediate failures through their
normal completion path. Do not rely on a callback running inline with the
initiating call.

## Own data until completion

Unless an overload explicitly states that it copies or owns an argument:

- buffers, spans, string views, references, pointers, and their targets are
  borrowed;
- the I/O object and every borrowed argument must remain alive until completion;
- cancelling an operation starts completion; it does not immediately release
  borrowed state;
- a detached coroutine must own captured state or otherwise prove that each
  reference outlives it;
- an object returned by a parent may depend on the parent, its connection, or
  its executor.

Detached response writes and other explicitly owning overloads are exceptions,
not a default ownership rule.

## Serialize a stateful object

Unless a type documents a stronger guarantee:

- distinct objects may be used concurrently;
- one shared stateful object is not implicitly thread-safe;
- initiation, handlers, cancellation, close, and destruction for one logical
  object must be serialized;
- protecting only the initiating calls with a mutex is insufficient if handlers
  can still run concurrently.

When multiple threads run the same `io_context`, keep one logical service on
one strand:

```cpp
asio::io_context context;
auto strand = asio::make_strand(context);
riwo::websocket::client client(strand);

asio::co_spawn(strand, [&]() -> riwo::awaitable<void>
{
    auto stream = co_await client.open(
        "ws://127.0.0.1:8080/echo", riwo::use_awaitable);

    // Keep later client and stream access on the same strand.
    co_await stream.close(riwo::use_awaitable);
}, riwo::detached);
```

Binding only the final handler to a strand does not retroactively serialize
other code that accesses the same object. Initiate all related work from that
strand.

## Per-object operation limits

| Area | Limit for one object |
| --- | --- |
| Core scheduling | `post` and `dispatch` follow the supplied executor; captured state remains the caller's responsibility |
| Coroutine primitive | Multiple waiters are supported; the primitive must outlive every waiter |
| HTTP connection | At most one read and one write may be active; one of each may overlap |
| HTTP client/server/context | Serialize state transitions and access |
| WebSocket stream | One read-family operation may be active; one read and one write may overlap; message writes use a bounded queue |
| Process | At most one stdin write, one stdout read, and one stderr read may be active |
| INI/settings file I/O | File work may run on a worker executor; serialize access to mutable object state |
| Signal/soft-bus delivery | Delivery may be queued, but views, references, and callback captures still need valid lifetimes |

An internal queue means that a documented operation is queued. It does not
grant arbitrary concurrent access to the containing object.

## Cancel and shut down in order

For an owned runtime, use this order:

1. Stop initiating new work.
2. Cancel or close active protocol, timer, process, and wait operations.
3. Keep objects and borrowed state alive while completions drain.
4. Destroy dependent objects after their final completions.
5. Stop and join the executor threads last.

For the process-wide runtime, `riwo::exit()` stops the loop. It is not a
substitute for application-level protocol close, buffer ownership, or cleanup
that must happen before the loop stops.
