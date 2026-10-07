# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.15)

foreach(required_variable
	TEST_PROBE_SOURCE_DIR
	TEST_PROBE_COMPONENT
	TEST_PROBE_EXPECT_FOUND
)
	if (NOT DEFINED ${required_variable})
		message(FATAL_ERROR "Missing ${required_variable}.")
	endif ()
endforeach()

# Configure the real project first so the probe exercises the generated
# RiwoConfig.cmake rather than a test-only approximation.
include("${CMAKE_CURRENT_LIST_DIR}/run_configure.cmake")

set(probe_binary_dir "${TEST_BINARY_DIR}-probe")
file(REMOVE_RECURSE "${probe_binary_dir}")

# install(EXPORT) writes RiwoTargets.cmake only while installing.  Dependency
# selection happens before that include, so an empty file is sufficient for a
# configure-only package probe and keeps these regression tests inexpensive.
file(WRITE "${TEST_BINARY_DIR}/RiwoTargets.cmake"
	"# Deliberately empty: dependency-selection probe.\n"
)
set(probe_command
	"${CMAKE_COMMAND}"
	-S "${TEST_PROBE_SOURCE_DIR}"
	-B "${probe_binary_dir}"
	-G "${TEST_GENERATOR}"
)
if (TEST_GENERATOR_PLATFORM)
	list(APPEND probe_command -A "${TEST_GENERATOR_PLATFORM}")
endif ()

if (TEST_GENERATOR_TOOLSET)
	list(APPEND probe_command -T "${TEST_GENERATOR_TOOLSET}")
endif ()

if (TEST_MAKE_PROGRAM)
	list(APPEND probe_command "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
endif ()

if (TEST_CXX_COMPILER)
	list(APPEND probe_command "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
endif ()

if (TEST_RC_COMPILER)
	list(APPEND probe_command "-DCMAKE_RC_COMPILER=${TEST_RC_COMPILER}")
endif ()

if (TEST_MT)
	list(APPEND probe_command "-DCMAKE_MT=${TEST_MT}")
endif ()

if (TEST_TOOLCHAIN_FILE)
	list(APPEND probe_command "-DCMAKE_TOOLCHAIN_FILE=${TEST_TOOLCHAIN_FILE}")
endif ()

list(APPEND probe_command
	"-DRiwo_DIR=${TEST_BINARY_DIR}"
	"-DPROBE_COMPONENT=${TEST_PROBE_COMPONENT}"
	"-DPROBE_EXPECT_FOUND=${TEST_PROBE_EXPECT_FOUND}"
	"-DPROBE_EXPECT_ERROR=${TEST_PROBE_EXPECT_ERROR}"
)
if (TEST_PROBE_OPTIONS)
	string(REPLACE "|" ";" probe_options "${TEST_PROBE_OPTIONS}")
	foreach(option IN LISTS probe_options)
		list(APPEND probe_command "-D${option}")
	endforeach()
endif ()

execute_process (
	COMMAND ${probe_command}
	RESULT_VARIABLE probe_result
	OUTPUT_VARIABLE probe_stdout
	ERROR_VARIABLE probe_stderr
)
if (NOT probe_result EQUAL 0)
	message(FATAL_ERROR
		"Package probe '${TEST_NAME}' failed.\n${probe_stdout}\n${probe_stderr}"
	)
endif ()

message(STATUS "Package probe '${TEST_NAME}' passed.")
