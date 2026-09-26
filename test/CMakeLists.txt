# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(RIWO_TEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(RIWO_TEST_LAUNCHER)

# GCC's ThreadSanitizer cannot reserve its shadow-memory layout with the more
# aggressive address randomisation used by some recent Linux kernels.  Probe a
# per-process ASLR-disabled launcher instead of requiring developers to know the
# platform-specific setarch workaround.  The personality is inherited by
# interoperability peers spawned by Python as well.
if (RIWO_ENABLE_TEST_TSAN AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_program(RIWO_TEST_SETARCH_EXECUTABLE NAMES setarch)
	if (RIWO_TEST_SETARCH_EXECUTABLE)
		execute_process (
			COMMAND ${RIWO_TEST_SETARCH_EXECUTABLE}
				${CMAKE_SYSTEM_PROCESSOR} -R ${CMAKE_COMMAND} -E true
			RESULT_VARIABLE riwo_test_setarch_result
			OUTPUT_QUIET ERROR_QUIET
		)
		if (riwo_test_setarch_result EQUAL 0)
			set(RIWO_TEST_LAUNCHER
				${RIWO_TEST_SETARCH_EXECUTABLE} ${CMAKE_SYSTEM_PROCESSOR} -R
			)
			message(STATUS
				"${PRO_NAME}: TSan tests disable ASLR per process with setarch."
			)
		endif ()
	endif ()
	mark_as_advanced(RIWO_TEST_SETARCH_EXECUTABLE)
endif ()


function(add_riwo_ctest test_name)
	add_test(NAME ${test_name} COMMAND ${RIWO_TEST_LAUNCHER} ${ARGN})
	# CTest may be launched from an environment whose PATH contains another
	# MinGW distribution.  Put the runtime matching CMAKE_CXX_COMPILER first for
	# every suite, not just the functional tests, so DLL resolution is stable.
	if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.22 AND riwo_gnu_runtime_dir)
		set_property(TEST ${test_name} APPEND PROPERTY
			ENVIRONMENT_MODIFICATION
			"PATH=path_list_prepend:${riwo_gnu_runtime_dir}"
		)
	endif ()
endfunction()


if(CMAKE_COMPILER_IS_GNUCXX)
	include(CheckCXXCompilerFlag)
	check_cxx_compiler_flag("-Wa,-mbig-obj" RIWO_TEST_GNU_BIG_OBJ)
endif()


# Build a test program without necessarily registering it as a standalone
# CTest test. Interoperability runners use this for the Riwo peer process.
function(add_riwo_test_program test_target source library)
	add_executable(${test_target} ${source})
	target_link_libraries(${test_target} PRIVATE ${library})

	if ("${library}" MATCHES "^riwo\\.(http|websocket)$")
		riwo_limit_heavy_compile(${test_target})
	endif ()

	target_compile_options(${test_target} PRIVATE
		$<$<AND:$<CXX_COMPILER_ID:GNU>,$<BOOL:${RIWO_TEST_GNU_BIG_OBJ}>>:-Wa,-mbig-obj>
	)
	if (RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN)
		target_link_libraries(${test_target} PRIVATE riwo.test.sanitizer)
	endif ()

	target_include_directories(${test_target} PRIVATE ${RIWO_TEST_SOURCE_DIR})

	set_target_properties(${test_target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${RIWO_OUTPUT_DIR}/bin
	)
endfunction()


function(configure_riwo_sanitized_test test_name)
	if (RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN)
		set_property(TEST ${test_name} APPEND PROPERTY LABELS sanitizer)
		set_property(TEST ${test_name} PROPERTY RUN_SERIAL TRUE)

		set_property(TEST ${test_name} APPEND PROPERTY FAIL_REGULAR_EXPRESSION
			"AddressSanitizer" "LeakSanitizer" "ThreadSanitizer" "runtime error:"
		)
		if (RIWO_ENABLE_TEST_SANITIZERS)
			set(riwo_asan_options
				"detect_leaks=0:halt_on_error=1:abort_on_error=1"
			)
			# Ubuntu's libc++abi 18 frees standard exception messages through
			# free(), while compiler-rt records their allocation as operator new.
			# A standalone throw/catch reproduces this runtime mismatch, so do not
			# treat it as a project allocation error in libc++ sanitizer builds.
			if (RIWO_USE_LIBCXX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
				string(APPEND riwo_asan_options ":alloc_dealloc_mismatch=0")
			endif ()

			set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
				"ASAN_OPTIONS=${riwo_asan_options}"
				"UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
			)
		else ()
			set(riwo_tsan_options "halt_on_error=1")
			# Distribution libc++ is not TSan-instrumented. Its future and
			# condition-variable internals otherwise produce false races (and can
			# stall waits), while project code remains fully instrumented.
			if (RIWO_USE_LIBCXX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
				string(APPEND riwo_tsan_options
					":ignore_noninstrumented_modules=1")
			endif ()

			set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
				"TSAN_OPTIONS=${riwo_tsan_options}"
			)
		endif ()
	endif ()
endfunction()


function(configure_riwo_functional_test test_name)
	set_tests_properties(${test_name} PROPERTIES
		LABELS functional
		TIMEOUT ${RIWO_FUNCTIONAL_TIMEOUT}
	)
	set_property(TEST ${test_name} APPEND PROPERTY ENVIRONMENT
		"RIWO_TEST_REPEAT=${RIWO_FUNCTIONAL_REPEAT}"
		"RIWO_TEST_SEED=${RIWO_FUNCTIONAL_SEED}"
		"RIWO_TEST_FAIL_FAST=1"
	)
	configure_riwo_sanitized_test(${test_name})
endfunction()


# Functional tests keep their historical target and CTest names so existing
# per-test invocations continue to work.
function(add_riwo_functional_test name source library)
	set(test_target riwo.test.${name})
	add_riwo_test_program(${test_target} ${source} ${library})

	add_riwo_ctest(riwo.${name} $<TARGET_FILE:${test_target}> ${ARGN})
	configure_riwo_functional_test(riwo.${name})
endfunction()


# Compatibility wrapper for out-of-tree tests using the old helper name.
function(add_riwo_test name source library)
	add_riwo_functional_test(${name} ${source} ${library} ${ARGN})
endfunction()


function(add_riwo_stress_test name source library)
	set(test_target riwo.test.stress.${name})
	add_riwo_test_program(${test_target} ${source} ${library})

	target_compile_definitions(${test_target} PRIVATE
		RIWO_STRESS_SCALE=${RIWO_STRESS_SCALE}
	)
	add_riwo_ctest(riwo.stress.${name} $<TARGET_FILE:${test_target}>
		--repeat ${RIWO_STRESS_REPEAT}
		--seed ${RIWO_STRESS_SEED}
		--fail-fast
	)
	set_tests_properties(riwo.stress.${name} PROPERTIES
		LABELS stress
		RUN_SERIAL TRUE
		TIMEOUT ${RIWO_STRESS_TIMEOUT}
	)
	configure_riwo_sanitized_test(riwo.stress.${name})
endfunction()


function(add_riwo_performance_test name source library)
	set(test_target riwo.test.performance.${name})
	add_riwo_test_program(${test_target} ${source} ${library})

	target_compile_definitions(${test_target} PRIVATE
		RIWO_PERFORMANCE_SCALE=${RIWO_PERFORMANCE_SCALE}
	)
	add_riwo_ctest(riwo.performance.${name} $<TARGET_FILE:${test_target}>)

	set_tests_properties(riwo.performance.${name} PROPERTIES
		LABELS performance
		RUN_SERIAL TRUE
		TIMEOUT ${RIWO_PERFORMANCE_TIMEOUT}
	)
endfunction()


if (RIWO_BUILD_FUZZERS)
	add_subdirectory(fuzz)
	message(STATUS
		"${PRO_NAME}: Other test suites disabled in the dedicated fuzzer build."
	)
else ()
	add_subdirectory(functional)

	if (RIWO_BUILD_STRESS_TESTS)
		add_subdirectory(stress)
	endif ()

	if (RIWO_BUILD_PERFORMANCE_TESTS)
		add_subdirectory(performance)
	endif ()

	if (NOT RIWO_BUILD_STRESS_TESTS AND NOT RIWO_BUILD_PERFORMANCE_TESTS)
		message(STATUS
			"${PRO_NAME}: Enable RIWO_BUILD_STRESS_TESTS or "
			"RIWO_BUILD_PERFORMANCE_TESTS for opt-in non-functional suites."
		)
	endif ()
endif ()

if (RIWO_BUILD_CMAKE_TESTS)
	add_subdirectory(cmake)
endif ()
