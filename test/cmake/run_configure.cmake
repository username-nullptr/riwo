# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.15)

foreach(required_variable
	TEST_NAME
	TEST_SOURCE_DIR
	TEST_BINARY_DIR
	TEST_GENERATOR
	TEST_EXPECT_SUCCESS
)
	if (NOT DEFINED ${required_variable})
		message(FATAL_ERROR "Missing ${required_variable}.")
	endif ()
endforeach()

file(REMOVE_RECURSE "${TEST_BINARY_DIR}")

set(configure_command
	"${CMAKE_COMMAND}"
	-S "${TEST_SOURCE_DIR}"
	-B "${TEST_BINARY_DIR}"
	-G "${TEST_GENERATOR}"
)
if (TEST_GENERATOR_PLATFORM)
	list(APPEND configure_command -A "${TEST_GENERATOR_PLATFORM}")
endif ()

if (TEST_GENERATOR_TOOLSET)
	list(APPEND configure_command -T "${TEST_GENERATOR_TOOLSET}")
endif ()

if (TEST_MAKE_PROGRAM)
	list(APPEND configure_command "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
endif ()

if (TEST_C_COMPILER)
	list(APPEND configure_command "-DCMAKE_C_COMPILER=${TEST_C_COMPILER}")
endif ()

if (TEST_CXX_COMPILER)
	list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
endif ()

if (TEST_TOOLCHAIN_FILE)
	list(APPEND configure_command "-DCMAKE_TOOLCHAIN_FILE=${TEST_TOOLCHAIN_FILE}")
endif ()

if (TEST_BUILD_TYPE)
	list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${TEST_BUILD_TYPE}")
endif ()

list(APPEND configure_command
	-DBUILD_TESTING=OFF
	-DRIWO_BUILD_CMAKE_TESTS=OFF
	-DRIWO_BUILD_EXAMPLES=OFF
)
if (TEST_OPTIONS)
	string(REPLACE "|" ";" test_options "${TEST_OPTIONS}")

	foreach(option IN LISTS test_options)
		list(APPEND configure_command "-D${option}")
	endforeach()
endif ()

execute_process (
	COMMAND ${configure_command}
	RESULT_VARIABLE configure_result
	OUTPUT_VARIABLE configure_stdout
	ERROR_VARIABLE configure_stderr
)
set(configure_output "${configure_stdout}\n${configure_stderr}")

if (TEST_EXPECT_SUCCESS)
	if (NOT configure_result EQUAL 0)
		message(FATAL_ERROR
			"Configuration '${TEST_NAME}' unexpectedly failed:\n${configure_output}"
		)
	endif ()

	set(package_config "${TEST_BINARY_DIR}/RiwoConfig.cmake")

	if (NOT EXISTS "${package_config}")
		message(FATAL_ERROR
			"Configuration '${TEST_NAME}' did not generate RiwoConfig.cmake."
		)
	endif ()

	file(READ "${package_config}" package_config_contents)
	string(REPLACE "," ";" expected_components "${TEST_EXPECT_COMPONENTS}")

	if (NOT package_config_contents MATCHES "set\\(Riwo_core_FOUND TRUE\\)")
		message(FATAL_ERROR "The generated package does not expose core.")
	endif ()

	foreach(component coro http websocket utils)
		if (component IN_LIST expected_components)
			set(expected_value ON)
		else ()
			set(expected_value OFF)
		endif ()

		if (NOT package_config_contents MATCHES
			"set\\(Riwo_${component}_FOUND ${expected_value}\\)")
			message(FATAL_ERROR
				"Configuration '${TEST_NAME}' generated an inconsistent "
				"${component} component state; expected ${expected_value}."
			)
		endif ()
	endforeach()

	message(STATUS "Configuration '${TEST_NAME}' succeeded as expected.")
else ()
	if (configure_result EQUAL 0)
		message(FATAL_ERROR
			"Configuration '${TEST_NAME}' unexpectedly succeeded."
		)
	endif ()

	if (TEST_EXPECT_ERROR AND NOT configure_output MATCHES "${TEST_EXPECT_ERROR}")
		message(FATAL_ERROR
			"Configuration '${TEST_NAME}' failed for the wrong reason.\n"
			"Expected: ${TEST_EXPECT_ERROR}\nActual output:\n${configure_output}"
		)
	endif ()

	message(STATUS "Configuration '${TEST_NAME}' was rejected as expected.")
endif ()
