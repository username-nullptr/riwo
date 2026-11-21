# System

Language: English | [简体中文](../zh_CN/system.md)

| Property | Value |
| --- | --- |
| Build option | `RIWO_BUILD_SYSTEM=ON` (default) |
| Source-tree target | `riwo.system` |
| Installed target | `Riwo::system` |
| Umbrella header | `<riwo/system.h>` |
| Depends on | Core |

System owns optional operating-system services with resource or process-wide
state. Executable-relative path resolution remains in Core so modules do not
need System merely to resolve project resources.

## Header map

| Area | Preferred header | Main facilities |
| --- | --- | --- |
| Environment | `<riwo/system/environment.h>` | Get, enumerate, set, and unset environment variables |
| User | `<riwo/system/user.h>` | Current user, home directory, and `~` expansion |
| Application paths | `<riwo/system/paths.h>` | Locate per-user config, data, cache, and state directories |
| Process | `<riwo/system/process.h>` | Child lifecycle, wait, output capture, and standard-stream redirection |
| Temporary resources | `<riwo/system/temporary.h>` | Exclusive creation and scoped cleanup of files and directories |
| File lock | `<riwo/system/file_lock.h>` | Non-blocking exclusive locks across processes |
| Directory watcher | `<riwo/system/directory_watcher.h>` | Wait synchronously or asynchronously for changes to immediate directory entries |

Environment values are UTF-8 strings. A missing variable is represented by an
empty `std::optional`; system-call failures remain `sys_expected` errors.

`system::process` provides start/run, join, detach, terminate, kill,
cancellation, timeouts, child working directory/environment, single-instance
locks, and stdin/stdout/stderr I/O. Keep the process object alive while I/O is
pending. Serialize lifecycle changes with I/O initiation, and keep at most one
write, stdout read, and stderr read active for one process.

## Capture child output

```cpp
auto result = riwo::system::process::capture("git", {"--version"});
if (result && result->exit_code == 0)
    std::cout << result->stdout_data;
```

`capture()` runs the executable synchronously, drains stdout and stderr concurrently,
and closes stdin immediately after launch. The argument array preserves whitespace
and empty arguments. Nonzero exit codes are valid process results.
`process_capture_options` defaults to a 30-second timeout and keeps up to 1 MiB
per output stream. The pipe is still drained after its limit is reached, and the
corresponding truncation flag is set. A zero timeout waits indefinitely. A timeout
stops and reaps the direct child and returns `std::errc::timed_out`.

Call `set_stdio(process_stdio)` to set `input`, `output`, and `error` for
subsequent launches. Each stream defaults to `pipe` and also supports `inherit`,
`discard`, or `file`. File paths are relative to the parent process's working
directory. Input files must exist, and output files are created or truncated.
Passing the same file path for both output streams shares one open file.
After the last pipe write completes, `close_stdin()` sends EOF to the child.

## Directories and files

- `app_paths("org.example.tool")` resolves `config`, `data`, `cache`, and `state` paths. Callers can create the directories as needed. On Windows, configuration uses Roaming AppData and the other paths use Local AppData. On Linux, each path respects its XDG variable, ignores relative values, and falls back to the appropriate directory under Home. Application IDs start with an ASCII letter or digit and allow ASCII letters, digits, periods, underscores, and hyphens. Trailing periods and Windows device names are invalid.
- `temp_file::create()` and `temp_directory::create()` exclusively create a resource in the system temporary directory or an existing parent directory. The movable owner attempts cleanup on destruction. Call `remove()` to check cleanup errors or `release()` to keep the resource.
- `file_lock::try_lock(path)` returns the lock, an empty `std::optional` when the lock is busy, or a system error. Destruction or `unlock()` releases the lock. The lock file persists. Cooperating processes use the same path and should avoid removing or replacing the file while it is in use.

## Wait for directory changes

`directory_watcher<Exec>::open(executor, path)` takes an initial snapshot.
You can also pass an execution context by lvalue, such as `open(context, path)`.
The supplied executor or the context's executor must be usable to construct
`Exec`, which defaults to `asio::any_io_executor`. `get_executor()` returns
`Exec`. The `open(path)` overload uses the Riwo executor when it can construct
an `Exec`.

`wait()` waits for a batch of `created`, `modified`, or `removed` events with absolute
paths. Snapshots cover immediate entries and detect replacement under the same
name. The default polling interval is 100 milliseconds. Short-lived changes
between scans can be coalesced or missed.

- Synchronous waits scan on the calling thread and can run without a running executor. `wait()` and `wait(riwo::use_sync)` report errors as `std::system_error`. Use `wait(error_code)`, `as_tuple(use_sync)`, or `as_expected(use_sync)` for error-code, tuple, or `sys_expected` results.
- Synchronous waits accept a `std::chrono::duration` or `std::chrono::time_point`. A zero duration waits indefinitely. A negative duration or a past deadline performs one scan.
- `wait(token)` also accepts a callback, `use_awaitable`, `use_future`, `deferred`, `detached`, and asynchronous error, timeout, and cancellation adapters. Asynchronous scans run on the supplied executor. [Execution and I/O model](io-model.md)

Each watcher supports one active wait. Synchronous and asynchronous waits share
the last delivered snapshot. `cancel()` can wake a blocking wait from another
thread or cancel an asynchronous wait. Once cancellation completes, waiting can
resume.

## Path resolution

Use `riwo::executable::resolve()` from `<riwo/core/executable.h>` for paths
relative to the running executable. Use `system::expand_user_path()` only when
the API explicitly accepts user-home syntax.

See [System examples](../../examples/README.md#system).
