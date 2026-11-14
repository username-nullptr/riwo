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
| Process | `<riwo/system/process.h>` | Child lifecycle, async wait, and standard-stream I/O |

Environment values are UTF-8 strings. A missing variable is represented by an
empty `std::optional`; system-call failures remain `sys_expected` errors.

`system::process` provides start/run, join, detach, terminate, kill,
cancellation, timeouts, child working directory/environment, single-instance
locks, and stdin/stdout/stderr I/O. Keep the process object alive while I/O is
pending. Serialize lifecycle changes with I/O initiation, and keep at most one
write, stdout read, and stderr read active for one process.

Use `riwo::executable::resolve()` from `<riwo/core/executable.h>` for paths
relative to the running executable. Use `system::expand_user_path()` only when
the API explicitly accepts user-home syntax.

See [System examples](../../examples/README.md#system).
