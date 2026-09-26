# Coroutines

Language: English | [简体中文](../zh_CN/coroutines.md)

| Property | Value |
| --- | --- |
| Build option | `RIWO_BUILD_CORO=ON` (default) |
| Source-tree target | `riwo.coro` |
| Installed target | `Riwo::coro` |
| Umbrella header | `<riwo/coro.h>` |
| Depends on | Core |

Coroutines adds suspending synchronization and executor-switching helpers. It is
the required base for HTTP and Utilities.

## Start coroutine work

`riwo::dispatch()` and `riwo::post()` accept an awaitable or a callable that
returns one:

```cpp
#include <riwo/coro.h>

using namespace riwo::coro::literals;

int main()
{
    riwo::dispatch([]() -> riwo::awaitable<void>
    {
        co_await 250_ms;
        riwo::exit();
    });

    return riwo::exec();
}
```

The duration literals are `_y`, `_mon`, `_d`, `_h`, `_min`, `_s`,
`_ms`, `_us`, and `_ns`.

## Facilities

| Header/API | Purpose |
| --- | --- |
| `coro::mutex`, `coro::unique_lock` | Exclusive locking and RAII ownership |
| `coro::shared_mutex`, `coro::shared_lock` | Shared and exclusive locking |
| `coro::semaphore`, `coro::binary_semaphore` | Counting and binary permits |
| `coro::condition_variable` | Predicate waits, timed waits, notify one/all |
| `coro::wait()` | Await a future, thread, `riwo::jthread`, or Asio thread pool |
| `coro::goto_exec()` | Resume on another executor and return the previous executor |
| `coro::goto_thread()` | Resume on a worker thread |

Timed lock and semaphore operations yield a Boolean acquisition result.

## Ownership and ordering

- A synchronization primitive must outlive all queued and resumed waiters.
- Update a condition-variable predicate while holding its associated mutex.
- A detached coroutine must own captures that do not otherwise have a proven
  longer lifetime.
- Do not keep a lock across an executor switch unless the cross-executor
  ordering is deliberate.
- Cancel or complete pending waits before stopping the executor that must resume
  them.
- Use coroutine primitives only for coroutine waiters. Use Core thread locks
  when a blocking thread-level critical section is intended.

The shared executor, completion, cancellation, strand, and shutdown contract is
in [Execution and I/O model](io-model.md). Runnable usage is indexed under
[Coroutine examples](../../examples/README.md#coroutines).
