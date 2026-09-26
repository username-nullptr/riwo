# SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

option(BUILD_TESTING
	"-- ${PRO_NAME}: Build tests." OFF
)
option(RIWO_BUILD_CMAKE_TESTS
	"-- ${PRO_NAME}: Test CMake option constraints and the installed package."
	${BUILD_TESTING}
)
option(RIWO_ENABLE_TEST_SANITIZERS
	"-- ${PRO_NAME}: Enable ASan and, where available, UBSan for functional and stress tests." OFF
)
option(RIWO_ENABLE_TEST_TSAN
	"-- ${PRO_NAME}: Enable TSan for functional and stress tests." OFF
)
option(RIWO_BUILD_FUZZERS
	"-- ${PRO_NAME}: Build Clang libFuzzer API and input harnesses." OFF
)
option(RIWO_BUILD_STRESS_TESTS
	"-- ${PRO_NAME}: Build high-pressure stability tests." OFF
)
option(RIWO_BUILD_PERFORMANCE_TESTS
	"-- ${PRO_NAME}: Build performance-sensitive benchmarks." OFF
)
set(RIWO_FUNCTIONAL_REPEAT 3 CACHE STRING
	"Execution count for each Riwo functional test case (positive integer)."
)
set(RIWO_FUNCTIONAL_SEED 1 CACHE STRING
	"Base seed for reproducible Riwo functional tests (non-negative integer)."
)
set(RIWO_FUNCTIONAL_TIMEOUT 120 CACHE STRING
	"CTest timeout in seconds for each Riwo functional executable."
)
set(RIWO_STRESS_SCALE 5 CACHE STRING
	"Work multiplier for Riwo stress tests (positive integer)."
)
set(RIWO_STRESS_REPEAT 3 CACHE STRING
	"Fixture recreation count for each Riwo stress case (positive integer)."
)
set(RIWO_STRESS_SEED 1 CACHE STRING
	"Base seed for reproducible Riwo stress scheduling (non-negative integer)."
)
set(RIWO_STRESS_TIMEOUT 180 CACHE STRING
	"CTest timeout in seconds for each Riwo stress executable."
)
set(RIWO_FUZZ_SMOKE_RUNS 2048 CACHE STRING
	"Iterations per Riwo fuzz smoke test (positive integer)."
)
set(RIWO_FUZZ_SEED 1 CACHE STRING
	"Base seed for reproducible Riwo fuzz smoke tests (non-negative integer)."
)
set(RIWO_FUZZ_MAX_LENGTH 4096 CACHE STRING
	"Maximum input size for Riwo fuzz smoke tests (positive integer)."
)
set(RIWO_FUZZ_TIMEOUT 5 CACHE STRING
	"Per-input timeout in seconds for Riwo fuzz smoke tests."
)
set(RIWO_FUZZ_RSS_LIMIT_MB 1024 CACHE STRING
	"Memory limit in MiB for Riwo fuzz smoke tests."
)
set(RIWO_PERFORMANCE_SCALE 1 CACHE STRING
	"Work multiplier for Riwo performance tests (positive integer)."
)
set(RIWO_PERFORMANCE_TIMEOUT 60 CACHE STRING
	"CTest timeout in seconds for each Riwo performance executable."
)
foreach(option
	RIWO_FUNCTIONAL_REPEAT
	RIWO_FUNCTIONAL_TIMEOUT
	RIWO_STRESS_SCALE
	RIWO_STRESS_REPEAT
	RIWO_STRESS_TIMEOUT
	RIWO_FUZZ_SMOKE_RUNS
	RIWO_FUZZ_MAX_LENGTH
	RIWO_FUZZ_TIMEOUT
	RIWO_FUZZ_RSS_LIMIT_MB
	RIWO_PERFORMANCE_SCALE
	RIWO_PERFORMANCE_TIMEOUT
)
	if (NOT ${option} MATCHES "^[1-9][0-9]*$")
		message(FATAL_ERROR "${option} must be a positive integer.")
	endif ()
endforeach()

foreach(option RIWO_FUNCTIONAL_SEED RIWO_STRESS_SEED RIWO_FUZZ_SEED)
	if (NOT ${option} MATCHES "^[0-9]+$")
		message(FATAL_ERROR "${option} must be a non-negative integer.")
	endif ()
endforeach()

if (RIWO_ENABLE_TEST_SANITIZERS AND RIWO_ENABLE_TEST_TSAN)
	message(FATAL_ERROR
		"${PRO_NAME}: ASan/UBSan and TSan cannot be enabled together."
	)
endif ()

if (RIWO_BUILD_CMAKE_TESTS AND NOT BUILD_TESTING)
	message(FATAL_ERROR
		"${PRO_NAME}: CMake integration tests require BUILD_TESTING=ON."
	)
endif ()

if ((RIWO_BUILD_STRESS_TESTS OR RIWO_BUILD_PERFORMANCE_TESTS) AND NOT BUILD_TESTING)
	message(FATAL_ERROR
		"${PRO_NAME}: Stress and performance tests require BUILD_TESTING=ON."
	)
endif ()

if (RIWO_BUILD_PERFORMANCE_TESTS AND
	(RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN))
	message(FATAL_ERROR
		"${PRO_NAME}: Performance tests cannot be combined with test sanitizers."
	)
endif ()

if (RIWO_BUILD_FUZZERS)
	if (NOT BUILD_TESTING)
		message(FATAL_ERROR "${PRO_NAME}: Fuzzers require BUILD_TESTING=ON.")
	endif ()

	if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		message(FATAL_ERROR "${PRO_NAME}: Fuzzers require Clang libFuzzer.")
	endif ()

	if (RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN)
		message(FATAL_ERROR
			"${PRO_NAME}: Fuzzer instrumentation cannot be combined with test sanitizer options."
		)
	endif ()

	if (RIWO_BUILD_STRESS_TESTS OR RIWO_BUILD_PERFORMANCE_TESTS)
		message(FATAL_ERROR
			"${PRO_NAME}: Fuzzers use a dedicated build; disable stress and performance tests."
		)
	endif ()

	if (RIWO_BUILD_EXAMPLES)
		message(FATAL_ERROR
			"${PRO_NAME}: Fuzzers use a dedicated build; disable examples."
		)
	endif ()

	include(CheckCXXSourceCompiles)
	set(riwo_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
	set(riwo_saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")
	set(riwo_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
	set(riwo_fuzzer_source
		"#include <cstddef>\n#include <cstdint>\nextern \"C\" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) { return 0; }"
	)
	set(riwo_fuzzer_compile_flags
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
	)
	set(riwo_fuzzer_link_options -fsanitize=fuzzer,address,undefined)
	if (RIWO_USE_LIBCXX)
		list(APPEND riwo_fuzzer_compile_flags -stdlib=libc++)
		list(APPEND riwo_fuzzer_link_options -stdlib=libc++)
	endif ()
	if (RIWO_USE_LLD)
		list(APPEND riwo_fuzzer_link_options -fuse-ld=lld)
	endif ()
	string(JOIN " " riwo_fuzzer_compile_flags_string
		${riwo_fuzzer_compile_flags}
	)

	set(CMAKE_REQUIRED_FLAGS
		"${riwo_saved_required_flags} ${riwo_fuzzer_compile_flags_string}"
	)
	set(CMAKE_REQUIRED_LINK_OPTIONS
		${riwo_saved_required_link_options}
		${riwo_fuzzer_link_options}
	)
	unset(RIWO_FUZZER_INSTRUMENTATION_AVAILABLE CACHE)

	check_cxx_source_compiles(
		"${riwo_fuzzer_source}"
		RIWO_FUZZER_INSTRUMENTATION_AVAILABLE
	)

	# Some Linux compiler-rt packages build libFuzzer against libstdc++ even
	# when Clang and libc++ are installed together. In that case the driver can
	# compile the probe but cannot link the fuzzer runtime with libc++. Place the
	# packaged runtime after ASan and satisfy only its private ABI dependency.
	unset(RIWO_FUZZER_COMPAT_RUNTIME_AVAILABLE CACHE)
	unset(RIWO_FUZZER_RUNTIME_LIBRARY)
	if (NOT RIWO_FUZZER_INSTRUMENTATION_AVAILABLE AND
		RIWO_USE_LIBCXX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
		execute_process(
			COMMAND ${CMAKE_CXX_COMPILER} --print-runtime-dir
			OUTPUT_VARIABLE riwo_clang_runtime_dir
			OUTPUT_STRIP_TRAILING_WHITESPACE
			RESULT_VARIABLE riwo_clang_runtime_dir_result
			ERROR_QUIET
		)
		if (CMAKE_CXX_COMPILER_TARGET)
			set(riwo_clang_target "${CMAKE_CXX_COMPILER_TARGET}")
		else ()
			execute_process(
				COMMAND ${CMAKE_CXX_COMPILER} -dumpmachine
				OUTPUT_VARIABLE riwo_clang_target
				OUTPUT_STRIP_TRAILING_WHITESPACE
				RESULT_VARIABLE riwo_clang_target_result
				ERROR_QUIET
			)
		endif ()
		string(REGEX MATCH "^[^-]+" riwo_clang_runtime_arch
			"${riwo_clang_target}"
		)
		set(riwo_fuzzer_runtime_candidate
			"${riwo_clang_runtime_dir}/libclang_rt.fuzzer-${riwo_clang_runtime_arch}.a"
		)

		if (riwo_clang_runtime_dir_result EQUAL 0 AND
			EXISTS "${riwo_fuzzer_runtime_candidate}")
			set(riwo_fuzzer_compat_compile_flags
				-fsanitize=fuzzer-no-link,address,undefined
				-fno-omit-frame-pointer
				-stdlib=libc++
			)
			string(JOIN " " riwo_fuzzer_compat_compile_flags_string
				${riwo_fuzzer_compat_compile_flags}
			)
			set(CMAKE_REQUIRED_FLAGS
				"${riwo_saved_required_flags} ${riwo_fuzzer_compat_compile_flags_string}"
			)
			set(CMAKE_REQUIRED_LINK_OPTIONS
				${riwo_saved_required_link_options}
				-fsanitize=fuzzer-no-link,address,undefined
				-stdlib=libc++
			)
			if (RIWO_USE_LLD)
				list(APPEND CMAKE_REQUIRED_LINK_OPTIONS -fuse-ld=lld)
			endif ()
			set(CMAKE_REQUIRED_LIBRARIES
				"${riwo_fuzzer_runtime_candidate}" -Wl,-lstdc++
			)
			check_cxx_source_compiles(
				"${riwo_fuzzer_source}"
				RIWO_FUZZER_COMPAT_RUNTIME_AVAILABLE
			)
			if (RIWO_FUZZER_COMPAT_RUNTIME_AVAILABLE)
				set(RIWO_FUZZER_RUNTIME_LIBRARY
					"${riwo_fuzzer_runtime_candidate}"
				)
				set(RIWO_FUZZER_INSTRUMENTATION_AVAILABLE TRUE)
				message(STATUS
					"${PRO_NAME}: Use the libstdc++-built libFuzzer runtime with libc++."
				)
			endif ()
		endif ()
	endif ()
	set(CMAKE_REQUIRED_FLAGS "${riwo_saved_required_flags}")
	set(CMAKE_REQUIRED_LINK_OPTIONS ${riwo_saved_required_link_options})
	set(CMAKE_REQUIRED_LIBRARIES ${riwo_saved_required_libraries})

	if (NOT RIWO_FUZZER_INSTRUMENTATION_AVAILABLE)
		message(FATAL_ERROR
			"${PRO_NAME}: Clang libFuzzer is unavailable for the selected C++ runtime."
		)
	endif ()
endif ()

if ((RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN) AND RIWO_ENABLE_LTO)
	message(FATAL_ERROR
		"${PRO_NAME}: Disable RIWO_ENABLE_LTO for sanitizer builds."
	)
endif ()

if (RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN)
	if (NOT BUILD_TESTING)
		message(FATAL_ERROR
			"${PRO_NAME}: Test sanitizers require BUILD_TESTING=ON."
		)
	endif ()

	if (MSVC AND RIWO_ENABLE_TEST_TSAN)
		message(FATAL_ERROR
			"${PRO_NAME}: MSVC does not provide ThreadSanitizer."
		)
	endif ()

	if (NOT MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
		message(FATAL_ERROR
			"${PRO_NAME}: Test sanitizers require MSVC, GCC, or Clang."
		)
	endif ()

	# MSVC AddressSanitizer is incompatible with the run-time checks and
	# Edit-and-Continue flags that CMake may add to Debug configurations.  Keep
	# the cache entries intact for normal builds and shadow them only in this
	# sanitizer configure.  With CMP0141 NEW, newly-created targets take their
	# debug-information format from this variable instead of the flags below.
	if (MSVC)
		foreach(riwo_msvc_flags_var
			CMAKE_C_FLAGS_DEBUG
			CMAKE_C_FLAGS_RELWITHDEBINFO
			CMAKE_CXX_FLAGS_DEBUG
			CMAKE_CXX_FLAGS_RELWITHDEBINFO
		)
			if (DEFINED ${riwo_msvc_flags_var})
				set(riwo_msvc_flags "${${riwo_msvc_flags_var}}")

				string(REGEX REPLACE
					"(^|[ \t])(/|-)RTC[1csu]*([ \t]|$)" " "
					riwo_msvc_flags "${riwo_msvc_flags}"
				)
				string(REGEX REPLACE
					"(^|[ \t])/ZI([ \t]|$)" "\\1/Zi\\2"
					riwo_msvc_flags "${riwo_msvc_flags}"
				)
				string(STRIP "${riwo_msvc_flags}" riwo_msvc_flags)
				set(${riwo_msvc_flags_var} "${riwo_msvc_flags}")
			endif ()
		endforeach()

		if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
			set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT ProgramDatabase)
		endif ()
	endif ()

	include(CheckCXXSourceCompiles)
	set(riwo_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
	set(riwo_saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")

	if (MSVC)
		set(riwo_sanitizer_compile_options /fsanitize=address)
		set(riwo_sanitizer_link_options /INCREMENTAL:NO)

	elseif (RIWO_ENABLE_TEST_SANITIZERS)
		set(riwo_sanitizer_compile_options
			-fsanitize=address,undefined
			-fno-omit-frame-pointer
			-fno-sanitize-recover=all
		)
		set(riwo_sanitizer_link_options -fsanitize=address,undefined)

	else ()
		set(riwo_sanitizer_compile_options
			-fsanitize=thread
			-fno-omit-frame-pointer
			-fno-sanitize-recover=all
		)
		set(riwo_sanitizer_link_options -fsanitize=thread)
	endif ()

	string(JOIN " " riwo_sanitizer_compile_flags
		${riwo_sanitizer_compile_options}
	)
	set(CMAKE_REQUIRED_FLAGS
		"${riwo_saved_required_flags} ${riwo_sanitizer_compile_flags}"
	)
	set(CMAKE_REQUIRED_LINK_OPTIONS
		${riwo_saved_required_link_options} ${riwo_sanitizer_link_options}
	)
	unset(RIWO_TEST_SANITIZER_AVAILABLE CACHE)

	check_cxx_source_compiles (
		"int main() { return 0; }"
		RIWO_TEST_SANITIZER_AVAILABLE
	)
	set(CMAKE_REQUIRED_FLAGS "${riwo_saved_required_flags}")
	set(CMAKE_REQUIRED_LINK_OPTIONS ${riwo_saved_required_link_options})

	if (NOT RIWO_TEST_SANITIZER_AVAILABLE)
		message(FATAL_ERROR
			"${PRO_NAME}: Requested test sanitizer runtime is unavailable."
		)
	endif ()

	add_library(riwo.test.sanitizer INTERFACE)
	set_target_properties(riwo.test.sanitizer PROPERTIES
		EXPORT_NAME sanitizer
	)
	add_library(Riwo::sanitizer ALIAS riwo.test.sanitizer)
	install(TARGETS riwo.test.sanitizer EXPORT RiwoTargets)

	target_compile_options(riwo.test.sanitizer INTERFACE
		${riwo_sanitizer_compile_options}
	)
	target_link_options(riwo.test.sanitizer INTERFACE
		${riwo_sanitizer_link_options}
	)
endif ()
