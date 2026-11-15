# Core

Language: English | [简体中文](../zh_CN/core.md)

| Property | Value |
| --- | --- |
| Build | Always enabled |
| Source-tree target | `riwo.core` |
| Installed target | `Riwo::core` |
| Umbrella header | `<riwo/core.h>` |
| Depends on | Selected Asio provider; optional OpenSSL/liburing |

Core is the foundation for every other Riwo module. It owns the common runtime,
value and container types, algorithms, executable-relative paths, files, and
thread-level synchronization.

## Header map

| Area | Preferred headers | Main facilities |
| --- | --- | --- |
| Execution | `<riwo/core/execution.h>` | Default context, scheduling, timers, sleeps, async work |
| Values | `<riwo/core/value.h>`, `container.h`, string containers | Text-backed values, conversion, parameter containers |
| Structured input | `<riwo/core/url.h>`, `ini.h`, `args_parser.h` | URL, INI, command-line parsing |
| Algorithms | `<riwo/core/algorithm.h>`, `mime_type.h` | UUID, SHA-1, wildcard/encoding/math helpers, MIME lookup |
| Synchronization | `<riwo/core/atomic_mutex.h>`, `shared_mutex.h`, `lock_free_queue.h` | Thread locks and queues |
| Thread lifecycle | `<riwo/core/jthread.h>` | Joining thread, stop source/token/callback |
| Runtime foundation | `<riwo/core/executable.h>`, `library.h` | Executable-relative paths and dynamic libraries |
| C++ foundation | `<riwo/core/global.h>`, `cxx.h`, `cxx/...` | Common macros, concepts, traits, operators, and exception foundations |
| Optional values and formatting | `<riwo/core/optional.h>`, `expected.h`, `sys_expected.h`, `formatters.h` | Optional values, expected results, system-error results, and formatting adapters |
| Asio adapters | `<riwo/core/asio.h>`, `asio/...` | Asio provider, executor/buffer concepts, buffers, and endpoint wrappers |
| Serialization | `<riwo/core/serialization/endian.h>`, `codec.h` | Byte-order conversion and type codecs |
| Common primitives | `<riwo/core/string_tools.h>`, `flags.h`, `static_registration.h` | String helpers, type-safe flags, and static registration |

`<riwo/core.h>` aggregates the common data, parsing, algorithm,
synchronization, and system headers. Execution, value, MIME, lock-free queues,
and other specialized facilities have their own entry headers and should be
included directly.

`<riwo/core/global.h>` preserves the transitive C++ foundation and Asio adapter
chains through `<riwo/core/cxx.h>` and `<riwo/core/asio.h>`. It does not provide
`optional`, `expected`, `sys_expected`, or formatting adapters; include the
corresponding standalone header above when using one of those facilities.
`<riwo/core/formatters.h>` depends on `global.h`, not the other way around.

## Runtime

```cpp
#include <riwo/core/execution.h>

#include <chrono>

int main()
{
    using namespace std::chrono_literals;

    riwo::post(1s, [] { riwo::exit(); });
    return riwo::exec();
}
```

The process-wide runtime is convenient for small programs. Scheduling overloads
also accept application-owned executors. Completion tokens, cancellation,
lifetimes, strands, and shutdown order are defined in the
[Execution and I/O model](io-model.md).

## Data and input

- `value` stores text and provides checked conversions and formatting.
- Parameter and string containers supply the shared key/value vocabulary used
  by higher modules.
- `url` parses and resolves hierarchical URLs; protocol modules decide which
  schemes they accept.
- `ini` offers group/key access plus synchronous and token-based file I/O.
- `cmdline::args_parser` handles aliases, value options, combined flags,
  positional arguments, help, and version output.
- Algorithm headers provide reusable hashes, identifiers, matching, encoding,
  byte-order, and math helpers.
- MIME helpers inspect suffixes and content to classify files.

## Threads and system access

Use Core locks and queues when a thread may block. Use
[Coroutine](coroutines.md) primitives when waiting should suspend a coroutine.

`atomic_mutex` and `atomic_shared_mutex` use the balanced policy by default.
The low-latency policy spins and is appropriate only for short, bounded
critical sections on controlled threads.

`riwo::jthread`, `stop_token`, `stop_source`, and `stop_callback` use the
standard-library implementation where available; Core supplies a compatible
fallback otherwise.

`riwo::executable` provides the running executable path and resolves relative
paths against its directory. `riwo::library` loads shared libraries and
symbols; keep the library object alive while any resolved symbol is used.

## Boundaries

Core does not provide coroutine-aware locks, protocol clients/servers,
application logging, environment/user access, or process management. Those
belong to Coroutines, HTTP, WebSocket, Utilities, and System. OpenSSL and io_uring support change the
Core/Asio build capability; applications still own TLS policy and executor
lifecycle.

See [Core examples](../../examples/README.md#core) for runnable coverage.
