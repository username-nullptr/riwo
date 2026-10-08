# Utilities

Language: English | [简体中文](../zh_CN/utilities.md)

| Property | Value |
| --- | --- |
| Build option | `RIWO_BUILD_UTILITIES=ON` (default OFF) |
| Source-tree target | `riwo.utils` |
| Installed target | `Riwo::utils` |
| Umbrella header | `<riwo/utils.h>` |
| Depends on | Coroutines and spdlog |
| Optional capability | UDP soft bus with `RIWO_BUILD_UTILITIES_SBUS_UDP` |

Utilities contains application-level facilities that do not belong to the
protocol stack.

## Header map

| Area | Preferred header | Main facilities |
| --- | --- | --- |
| Logging | `<riwo/utils/logger.h>` | Named console and rotating-file loggers |
| Settings | `<riwo/utils/settings.h>` | Named INI-backed settings and change signals |
| Signals | `<riwo/utils/signal_slot.h>` | Synchronous, asynchronous, and backpressure delivery |
| Observers | `<riwo/utils/observer.h>` | ID-addressed callbacks dispatched by executor |
| Modules | `<riwo/utils/modules.h>` | Dependency-ordered application initialization |
| Processes | `<riwo/utils/process.h>` | Child lifecycle and standard-stream I/O |
| Soft bus | `<riwo/utils/sbus.h>` | Typed publish/subscribe and cached topic state |

The umbrella header includes logger, settings, modules, and soft bus. Include
process, signal/slot, or observer directly when using those facilities.

## Logging and settings

`utils::logger` manages named console and file loggers. Configuration covers
paths, levels, rotation, timestamps, and formatting. File sinks write
asynchronously; call `logger::flush()` when persistence must be observed before
continuing or exiting.

`utils::settings` wraps `riwo::ini` as a named instance and exposes
`changed` and `loaded` signals. Use `get()`/`set()` for values and
`ini()` when direct persistence control is required.

## Signals, observers, and module startup

`utils::signal<Signature>` supports three delivery modes:

| Mode | Delivery |
| --- | --- |
| `sync` | Invoke slots in the caller |
| `async` | Queue slots on an executor |
| `backpressure` | Queue on an executor and block until delivery completes |

Do not invoke backpressure delivery from the same executor thread that must run
the slots. Dispatch owns value arguments, but views, pointers, references, and
objects captured by slots remain the caller's responsibility.

`utils::observer` routes indexed callbacks by stable object ID and unregisters
on destruction. `utils::modules` registers named initializers, orders them by
declared dependencies, and supports synchronous or asynchronous initialization.

## Processes

`utils::process` provides start/run, join, detach, terminate, kill,
cancellation, timeouts, working directory/environment, single-instance locks,
and stdin/stdout/stderr I/O. Commands and arguments use
`std::filesystem::path`.

Keep the process object alive while I/O is pending. Serialize lifecycle changes
with I/O initiation. For one process, keep at most one stdin write, one stdout
read, and one stderr read active.

## Soft bus

The soft bus separates typed publish/subscribe/cache APIs from transport:

| Transport | Types | Availability |
| --- | --- | --- |
| In-process | `local_interface`, `local_subscriber`, `local_cache` | Always within Utilities |
| UDP multicast | `udp_interface`, `udp_subscriber`, `udp_cache` | `RIWO_BUILD_UTILITIES_SBUS_UDP=ON` |
| Custom | `basic_subscriber<Interface>`, `cache<Subscriber>`, `publish<Interface>()` | User implementation |

`RIWO_UTILS_SBUS_DEFAULT_INTERFACE` selects `local` (default) or `udp` for
unqualified APIs. Name the concrete transport in code when behavior must not
change with the build configuration.

`cache::wait_changed()` waits for the next change after initiation. It is
edge-triggered and does not replay an earlier change. Use `changed()` for a
persistent subscription. `cache::cancel()` cancels current waits without
disconnecting persistent subscriptions or preventing later waits.

The UDP transport is versioned, fragmented, rate-limited, and bounded in source
tracking, reassembly, and callback delivery. It supports process, LAN, and
routed multicast scopes. Delivery is best effort: overload protection may drop
traffic, and the transport does not provide authentication, encryption, or
reliable replay. Configure network policy and ACLs outside Riwo.

`utils::thread_pool()` returns the module's shared Asio thread pool. Prefer an
application-owned executor when isolation or deterministic shutdown order
matters.

The shared ownership and concurrency contract is in
[Execution and I/O model](io-model.md). Runnable logging, settings, signals,
observers, processes, modules, and bus programs are indexed in
[Utilities examples](../../examples/README.md#utilities).
