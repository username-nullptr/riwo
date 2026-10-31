# Riwo Tests

This is the canonical guide for verification builds, CTest selection, runner
controls, and test-only CMake options. Tests are added only for library modules
enabled in the same build.

## Choose a suite

| Suite | Question it answers | Enable | CTest label |
| --- | --- | --- | --- |
| Functional | Does public behavior work deterministically? | `BUILD_TESTING=ON` | `functional` |
| CMake | Do option constraints, exports, and installed consumers work? | `RIWO_BUILD_CMAKE_TESTS=ON` | `cmake` |
| Interoperability | Does HTTP/WebSocket work with an available independent implementation? | discovered within Functional | `interop` |
| Stress | Does correctness survive concurrency, saturation, and repeated lifecycle work? | `RIWO_BUILD_STRESS_TESTS=ON` | `stress` |
| Fuzz | Do malformed inputs and call sequences expose failures? | `RIWO_BUILD_FUZZ_TESTS=ON` | `fuzz` |
| Performance | What throughput and latency does this build produce? | `RIWO_BUILD_PERFORMANCE_TESTS=ON` | `performance` |
| Sanitizer | Do Functional/Stress runs expose memory, UB, or race failures? | sanitizer option below | `sanitizer` |

Put successful behavior, invalid input, ownership, cancellation, timeouts, and
state transitions in Functional. Use Stress for pressure-dependent correctness,
Fuzz for broad input/state exploration, and Performance only for measurements.

## Functional profile

The following enables every module so every functional area is present:

```sh
cmake -S . -B build-test -DBUILD_TESTING=ON \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_EXP_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_EXP_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-test --parallel
ctest --test-dir build-test -L functional --output-on-failure
```

CTest names are `riwo.<area>`; executables are
`build-test/output/bin/riwo.test.<area>`. The
[functional API coverage map](functional/API_COVERAGE.md) records which source
owns each public behavior.

## CMake and installation profile

`RIWO_BUILD_CMAKE_TESTS` is disabled by default. Enable it together with
`BUILD_TESTING` to check:

- every Core/Coroutines/HTTP/WebSocket/Utilities enablement combination;
- rejection of invalid module, provider, sanitizer, fuzz, and numeric options;
- generated package component availability;
- dependency restoration for only the requested installed components;
- installation followed by an independent `find_package(Riwo)` consumer build
  and execution against both `Riwo::` and compatibility targets.

```sh
cmake -S . -B build-cmake-test -DBUILD_TESTING=ON \
  -DRIWO_BUILD_CMAKE_TESTS=ON
cmake --build build-cmake-test --parallel
ctest --test-dir build-cmake-test -L cmake --output-on-failure
```

The install-consumer test uses an isolated prefix below the build tree and does
not modify a system installation.

## Interoperability checks

Loopback tests are always the dependency-free baseline. When Python 3.8+ is
already available and the build is not cross-compiling, CMake may add external
checks without downloading dependencies:

- HTTP uses local `curl` when present, otherwise Python's standard library.
- WebSocket selects the first available backend among Node.js `ws`, Python
  `websockets`, Python `websocket-client`, and `wscat`.

Run only discovered interoperability entries with:

```sh
ctest --test-dir build-test -L interop --output-on-failure
```

## Stress profile

```sh
cmake -S . -B build-stress -DBUILD_TESTING=ON \
  -DRIWO_BUILD_STRESS_TESTS=ON \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_EXP_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_EXP_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-stress --parallel
ctest --test-dir build-stress -L stress --output-on-failure
```

CTest names are `riwo.stress.<area>`. Entries run serially at the CTest level;
the test executables create their own internal concurrency. Coverage includes
Core queues/locks and fallback joining threads, coroutine synchronization,
repeated HTTP/WebSocket and WebSocket_EXP connections, EXP_HTTP HTTP/1 pool
concurrency and HTTP/2 multiplexing with a stalled consumer, utility
lifecycle/fanout, and optional UDP soft-bus pressure.

## Fuzz profile

Fuzz testing uses a dedicated build. Clang enables libFuzzer with ASan + UBSan;
native MSVC enables its experimental libFuzzer + ASan support. GCC has no
libFuzzer runtime, so its build runs the same harnesses through a deterministic
standalone driver with ASan + UBSan. The profile does not add Functional or
Performance sources. Clang builds may additionally enable the Stress suite.

```sh
cmake -S . -B build-fuzz -DBUILD_TESTING=ON \
  -DRIWO_BUILD_FUZZ_TESTS=ON \
  -DRIWO_BUILD_STRESS_TESTS=ON \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_EXP_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-fuzz --parallel
ctest --test-dir build-fuzz -L fuzz --output-on-failure
ctest --test-dir build-fuzz -L stress --output-on-failure
```

Native MSVC uses `/fsanitize=fuzzer` and `/fsanitize=address`. From a Visual
Studio developer prompt:

```bat
cmake -S . -B build-msvc-fuzz -G Ninja ^
  -DBUILD_TESTING=ON -DRIWO_BUILD_FUZZ_TESTS=ON ^
  -DRIWO_BUILD_STATIC=ON ^
  -DRIWO_BUILD_HTTP=ON -DRIWO_BUILD_EXP_HTTP=ON ^
  -DRIWO_BUILD_WEBSOCKET=ON -DRIWO_BUILD_UTILITIES=ON ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-msvc-fuzz --parallel
ctest --test-dir build-msvc-fuzz -L fuzz --output-on-failure
```

MSVC fuzzing requires static Riwo libraries so the instrumented library code
and libFuzzer runtime are linked into one executable. It includes ASan but not
UBSan. Use `RelWithDebInfo`; unoptimized Debug + ASan can exceed the per-input
timeout even for small inputs. Keep a Clang fuzz build for undefined behavior
coverage and cross-toolchain validation.

GNU uses the same harness sources and seed corpora without requiring Clang.
Each target runs every corpus file, deterministic boundary inputs, and
`RIWO_FUZZ_SMOKE_RUNS` reproducible mutations. This is a regression and
compiler-compatibility test, not coverage-guided fuzzing:

```sh
cmake -S . -B build-gnu-fuzz -DBUILD_TESTING=ON \
  -DRIWO_BUILD_FUZZ_TESTS=ON \
  -DCMAKE_CXX_COMPILER=g++ \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_EXP_HTTP=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-gnu-fuzz --parallel
ctest --test-dir build-gnu-fuzz -L fuzz-regression --output-on-failure
```

Targets and CTest names are `riwo.fuzz.<module>.<harness>`; executables are
written to `build-fuzz/output/fuzz/`. Seed corpora and dictionaries live in
`test/fuzz/`. CMake copies corpora into the build tree and keeps crash
artifacts below `build-fuzz/test/fuzz/artifacts/`. GNU failures leave the exact
current input at `artifacts/<module>-<harness>/last-input`.

The EXP_HTTP HTTP/1 parser fuzzers compare contiguous and fragmented parsing
within the experimental codec itself. They require `RIWO_BUILD_EXP_HTTP=ON`
but do not build or link the legacy HTTP module.

Run a longer campaign directly:

```sh
build-fuzz/output/fuzz/riwo.fuzz.core.public-api \
  -max_total_time=300 \
  -artifact_prefix=build-fuzz/test/fuzz/artifacts/core-public-api/ \
  build-fuzz/test/fuzz/corpus/core-public-api/
```

Run a GNU regression with additional mutations directly:

```sh
build-gnu-fuzz/output/fuzz/riwo.fuzz.core.public-api \
  --corpus=build-gnu-fuzz/test/fuzz/corpus/core-public-api \
  --runs=10000 --seed=1 --max-len=4096 \
  --artifact=build-gnu-fuzz/test/fuzz/artifacts/core-public-api/last-input
```

## Performance profile

```sh
cmake -S . -B build-perf -DBUILD_TESTING=ON \
  -DRIWO_BUILD_PERFORMANCE_TESTS=ON \
  -DRIWO_BUILD_HTTP=ON \
  -DRIWO_BUILD_EXP_HTTP=ON \
  -DRIWO_HTTP_ZLIB_SUPPORT=ON \
  -DRIWO_EXP_HTTP_ZLIB_SUPPORT=ON \
  -DRIWO_BUILD_WEBSOCKET=ON \
  -DRIWO_BUILD_EXP_WEBSOCKET=ON \
  -DRIWO_BUILD_UTILITIES=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-perf --parallel
ctest --test-dir build-perf -L performance -V
```

CTest names are `riwo.performance.<area>`. The suite measures Core
algorithms/queues/locks, coroutine primitives, HTTP, WebSocket, WebSocket_EXP,
logging, signal/slot, and soft bus. When both HTTP implementations are enabled,
the `http-exp` test runs matched sequential and concurrent HTTP/1.1 loopback,
request/response head, and—when both implementations enable zlib—gzip
workloads. The gzip coverage includes 512 B and 64 KiB one-shot round trips,
reused codec state, and real HTTP compression-policy comparisons for dynamic
versus cached output, small-response bypass, and already-compressed MIME types.
It reports EXP_HTTP throughput relative to legacy HTTP as both a ratio and a
percentage change. The suite enforces correctness but has no fixed performance
threshold. Compare results only across the same host, compiler, build type,
feature set, and scale.

## Sanitizer profiles

| Option | Instrumentation | Constraint |
| --- | --- | --- |
| `RIWO_ENABLE_TEST_SANITIZERS=ON` | AddressSanitizer + UndefinedBehaviorSanitizer | GCC or Clang with GNU-style driver |
| `RIWO_ENABLE_TEST_SANITIZERS=ON` | AddressSanitizer | MSVC; x86 or x64 Windows 10+ |
| `RIWO_ENABLE_TEST_TSAN=ON` | ThreadSanitizer | GCC or Clang with GNU-style driver |

Example ASan/UBSan build:

```sh
cmake -S . -B build-asan -DBUILD_TESTING=ON \
  -DRIWO_ENABLE_TEST_SANITIZERS=ON \
  -DRIWO_BUILD_STRESS_TESTS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-asan --parallel
ctest --test-dir build-asan -L sanitizer --output-on-failure
```

MSVC uses the same option but enables AddressSanitizer only because MSVC does
not provide UndefinedBehaviorSanitizer. From a Visual Studio 2022 developer
prompt:

```bat
cmake -S . -B build-msvc-asan -G "Visual Studio 17 2022" -A x64 ^
  -DBUILD_TESTING=ON -DRIWO_ENABLE_TEST_SANITIZERS=ON
cmake --build build-msvc-asan --config RelWithDebInfo --parallel
ctest --test-dir build-msvc-asan -C RelWithDebInfo -L sanitizer --output-on-failure
```

The MSVC sanitizer profile disables incompatible run-time checks,
Edit-and-Continue, and incremental linking. MSVC does not instrument resumable
coroutine functions completely, so keep GCC or Clang sanitizer coverage for
coroutine-heavy code.

Use `RIWO_ENABLE_TEST_TSAN=ON` in a separate build. ASan/UBSan and TSan are
mutually exclusive. Both require `BUILD_TESTING=ON`, reject LTO, and cannot be
combined with the Performance suite. Fuzz instrumentation is a separate build
and cannot be combined with either sanitizer option. On Linux, TSan tests use
`setarch -R` when available to avoid incompatible shadow-memory layouts.

## Select tests and cases

List or filter CTest entries:

```sh
ctest --test-dir build-test -N
ctest --test-dir build-test -L functional --output-on-failure
ctest --test-dir build-test -R '^riwo\.http\.protocol$' --output-on-failure
```

Functional and Performance executables read runner environment variables.
Stress executables also receive equivalent command-line options:

| Environment | CLI | Meaning |
| --- | --- | --- |
| `RIWO_TEST_CASE` | `--case <name>` | Run an exact named case; may be repeated |
| `RIWO_TEST_REPEAT` | `--repeat <count>` | Recreate and rerun each selected fixture |
| `RIWO_TEST_SEED` | `--seed <value>` | Reproduce scheduling perturbations |
| `RIWO_TEST_FAIL_FAST=1` | `--fail-fast` | Stop after the first failed iteration |
| — | `--list` | List case names without running them |

Suite cache controls:

| Suite | Variables and defaults |
| --- | --- |
| Functional | `RIWO_FUNCTIONAL_REPEAT=3`, `RIWO_FUNCTIONAL_SEED=1`, `RIWO_FUNCTIONAL_TIMEOUT=120` |
| Stress | `RIWO_STRESS_SCALE=5`, `RIWO_STRESS_REPEAT=3`, `RIWO_STRESS_SEED=1`, `RIWO_STRESS_TIMEOUT=180` |
| Fuzz | `RIWO_FUZZ_SMOKE_RUNS=2048`, `RIWO_FUZZ_SEED=1`, `RIWO_FUZZ_MAX_LENGTH=4096`, `RIWO_FUZZ_TIMEOUT=5`, `RIWO_FUZZ_RSS_LIMIT_MB=1024` |
| Performance | `RIWO_PERFORMANCE_SCALE=1`, `RIWO_PERFORMANCE_TIMEOUT=60` |

Repeat, scale, length, timeout, and memory limits must be positive integers.
Seeds must be non-negative integers.

## Incompatible configurations

CMake rejects rather than silently weakening these requests:

- CMake tests, Stress, Performance, Fuzz, or sanitizers without
  `BUILD_TESTING=ON`;
- ASan/UBSan and TSan together;
- either sanitizer with LTO;
- Performance with either sanitizer;
- Fuzz without a supported compiler or available sanitizer runtime;
- Fuzz combined with sanitizer options, Performance, or examples;
- Fuzz combined with Stress under a compiler other than Clang.

Use a separate build directory for Functional, sanitizer, fuzz, and performance
profiles so their instrumentation and optimization settings do not mix.
