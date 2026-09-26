# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.15)

foreach(required_variable
	TEST_SOURCE_DIR
	TEST_BINARY_DIR
	TEST_MISSING_BINARY_DIR
	TEST_INSTALL_PREFIX
	TEST_INSTALL_CMAKEDIR
	RIWO_BINARY_DIR
	TEST_GENERATOR
	TEST_VERSION
	TEST_COMPONENTS
	TEST_MISSING_COMPONENT
	TEST_CTEST_COMMAND
)
	if (NOT DEFINED ${required_variable})
		message(FATAL_ERROR "Missing ${required_variable}.")
	endif ()
endforeach()

file(REMOVE_RECURSE
	"${TEST_BINARY_DIR}"
	"${TEST_MISSING_BINARY_DIR}"
	"${TEST_INSTALL_PREFIX}"
)

set(install_command
	"${CMAKE_COMMAND}" --install "${RIWO_BINARY_DIR}"
	--prefix "${TEST_INSTALL_PREFIX}"
)
if (TEST_CONFIGURATION)
	list(APPEND install_command --config "${TEST_CONFIGURATION}")
endif ()
execute_process(
	COMMAND ${install_command}
	RESULT_VARIABLE install_result
	OUTPUT_VARIABLE install_stdout
	ERROR_VARIABLE install_stderr
)
if (NOT install_result EQUAL 0)
	message(FATAL_ERROR
		"Installing Riwo failed. Build the project before running CTest.\n"
		"${install_stdout}\n${install_stderr}"
	)
endif ()

if (IS_ABSOLUTE "${TEST_INSTALL_CMAKEDIR}")
	set(riwo_package_dir "${TEST_INSTALL_CMAKEDIR}")
else ()
	set(riwo_package_dir
		"${TEST_INSTALL_PREFIX}/${TEST_INSTALL_CMAKEDIR}"
	)
endif ()

if (NOT EXISTS "${riwo_package_dir}/RiwoConfig.cmake")
	message(FATAL_ERROR
		"The install tree does not contain RiwoConfig.cmake at "
		"${riwo_package_dir}."
	)
endif ()

function(make_consumer_configure_command output_variable binary_dir)
	set(command
		"${CMAKE_COMMAND}"
		-S "${TEST_SOURCE_DIR}"
		-B "${binary_dir}"
		-G "${TEST_GENERATOR}"
	)
	if (TEST_GENERATOR_PLATFORM)
		list(APPEND command -A "${TEST_GENERATOR_PLATFORM}")
	endif ()

	if (TEST_GENERATOR_TOOLSET)
		list(APPEND command -T "${TEST_GENERATOR_TOOLSET}")
	endif ()

	if (TEST_MAKE_PROGRAM)
		list(APPEND command "-DCMAKE_MAKE_PROGRAM=${TEST_MAKE_PROGRAM}")
	endif ()

	if (TEST_CXX_COMPILER)
		list(APPEND command "-DCMAKE_CXX_COMPILER=${TEST_CXX_COMPILER}")
	endif ()

	if (TEST_TOOLCHAIN_FILE)
		list(APPEND command "-DCMAKE_TOOLCHAIN_FILE=${TEST_TOOLCHAIN_FILE}")
	endif ()

	if (TEST_BUILD_TYPE)
		list(APPEND command "-DCMAKE_BUILD_TYPE=${TEST_BUILD_TYPE}")
	endif ()

	list(APPEND command
		"-DRiwo_DIR=${riwo_package_dir}"
		"-DRIWO_EXPECTED_VERSION=${TEST_VERSION}"
		"-DRIWO_RUNTIME_DIR=${TEST_INSTALL_PREFIX}/bin"
	)
	set(${output_variable} ${command} PARENT_SCOPE)
endfunction()

make_consumer_configure_command(consumer_configure_command "${TEST_BINARY_DIR}")

list(APPEND consumer_configure_command
	"-DRIWO_EXPECTED_COMPONENTS=${TEST_COMPONENTS}"
)
execute_process (
	COMMAND ${consumer_configure_command}
	RESULT_VARIABLE consumer_configure_result
	OUTPUT_VARIABLE consumer_configure_stdout
	ERROR_VARIABLE consumer_configure_stderr
)
if (NOT consumer_configure_result EQUAL 0)
	message(FATAL_ERROR
		"Configuring the installed-package consumer failed.\n"
		"${consumer_configure_stdout}\n${consumer_configure_stderr}"
	)
endif ()

set(consumer_build_command
	"${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}" --parallel 2
)
if (TEST_CONFIGURATION)
	list(APPEND consumer_build_command --config "${TEST_CONFIGURATION}")
endif ()

execute_process (
	COMMAND ${consumer_build_command}
	RESULT_VARIABLE consumer_build_result
	OUTPUT_VARIABLE consumer_build_stdout
	ERROR_VARIABLE consumer_build_stderr
)
if (NOT consumer_build_result EQUAL 0)
	message(FATAL_ERROR
		"Building the installed-package consumer failed.\n"
		"${consumer_build_stdout}\n${consumer_build_stderr}"
	)
endif ()

set(consumer_test_command "${TEST_CTEST_COMMAND}" --output-on-failure)
if (TEST_CONFIGURATION)
	list(APPEND consumer_test_command -C "${TEST_CONFIGURATION}")
endif ()

execute_process(
	COMMAND ${consumer_test_command}
	WORKING_DIRECTORY "${TEST_BINARY_DIR}"
	RESULT_VARIABLE consumer_test_result
	OUTPUT_VARIABLE consumer_test_stdout
	ERROR_VARIABLE consumer_test_stderr
)
if (NOT consumer_test_result EQUAL 0)
	message(FATAL_ERROR
		"Running the installed-package consumer failed.\n"
		"${consumer_test_stdout}\n${consumer_test_stderr}"
	)
endif ()

# A package must also reject a component that was not installed.  Prefer a
# real disabled component, falling back to an unknown name in an all-module
# build.
make_consumer_configure_command(missing_configure_command
	"${TEST_MISSING_BINARY_DIR}"
)
list(APPEND missing_configure_command
	"-DRIWO_REQUEST_MISSING_COMPONENT=${TEST_MISSING_COMPONENT}"
)
execute_process (
	COMMAND ${missing_configure_command}
	RESULT_VARIABLE missing_configure_result
	OUTPUT_VARIABLE missing_configure_stdout
	ERROR_VARIABLE missing_configure_stderr
)
if (NOT missing_configure_result EQUAL 0)
	message(FATAL_ERROR
		"The negative component consumer did not configure cleanly.\n"
		"${missing_configure_stdout}\n${missing_configure_stderr}"
	)
endif ()

message(STATUS
	"Installed Riwo was consumed successfully with components: ${TEST_COMPONENTS}"
)
