# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(riwo_cmake_test_work_dir "${CMAKE_CURRENT_BINARY_DIR}/work")

function(add_riwo_configure_case name)
	cmake_parse_arguments(CASE "SUCCESS" "ERROR" "OPTIONS;COMPONENTS" ${ARGN})

	string(MD5 case_work_id "configure-${name}")
	string(SUBSTRING "${case_work_id}" 0 12 case_work_id)

	if (CASE_SUCCESS)
		set(case_expect_success ON)
	else ()
		set(case_expect_success OFF)
	endif ()

	string(JOIN "|" case_options ${CASE_OPTIONS})
	string(JOIN "," case_components ${CASE_COMPONENTS})

	add_test(NAME riwo.cmake.configure.${name}
		COMMAND ${CMAKE_COMMAND}
			"-DTEST_NAME=${name}"
			"-DTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
			"-DTEST_BINARY_DIR=${riwo_cmake_test_work_dir}/c-${case_work_id}"
			"-DTEST_GENERATOR=${CMAKE_GENERATOR}"
			"-DTEST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
			"-DTEST_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
			"-DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
			"-DTEST_C_COMPILER=${CMAKE_C_COMPILER}"
			"-DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
			"-DTEST_RC_COMPILER=${CMAKE_RC_COMPILER}"
			"-DTEST_MT=${CMAKE_MT}"
			"-DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
			"-DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
			"-DTEST_EXPECT_SUCCESS=${case_expect_success}"
			"-DTEST_EXPECT_ERROR=${CASE_ERROR}"
			"-DTEST_OPTIONS=${case_options}"
			"-DTEST_EXPECT_COMPONENTS=${case_components}"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/run_configure.cmake"
	)
	configure_riwo_ctest_environment(riwo.cmake.configure.${name})

	set_tests_properties(riwo.cmake.configure.${name} PROPERTIES
		LABELS "cmake;configure"
		TIMEOUT 120
	)
endfunction()

function(add_riwo_package_probe_case name)
	cmake_parse_arguments(CASE "FOUND" "COMPONENT;ERROR"
		"OPTIONS;COMPONENTS;PROBE_OPTIONS" ${ARGN}
	)
	string(MD5 case_work_id "package-${name}")
	string(SUBSTRING "${case_work_id}" 0 12 case_work_id)

	if (CASE_FOUND)
		set(case_expect_found ON)
	else ()
		set(case_expect_found OFF)
	endif ()

	string(JOIN "|" case_options ${CASE_OPTIONS})
	string(JOIN "," case_components ${CASE_COMPONENTS})
	string(JOIN "|" case_probe_options ${CASE_PROBE_OPTIONS})

	add_test(NAME riwo.cmake.package.${name}
		COMMAND ${CMAKE_COMMAND}
			"-DTEST_NAME=${name}"
			"-DTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
			"-DTEST_BINARY_DIR=${riwo_cmake_test_work_dir}/p-${case_work_id}"
			"-DTEST_PROBE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package_probe"
			"-DTEST_GENERATOR=${CMAKE_GENERATOR}"
			"-DTEST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
			"-DTEST_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
			"-DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
			"-DTEST_C_COMPILER=${CMAKE_C_COMPILER}"
			"-DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
			"-DTEST_RC_COMPILER=${CMAKE_RC_COMPILER}"
			"-DTEST_MT=${CMAKE_MT}"
			"-DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
			"-DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
			-DTEST_EXPECT_SUCCESS=ON
			"-DTEST_OPTIONS=${case_options}"
			"-DTEST_EXPECT_COMPONENTS=${case_components}"
			"-DTEST_PROBE_COMPONENT=${CASE_COMPONENT}"
			"-DTEST_PROBE_EXPECT_FOUND=${case_expect_found}"
			"-DTEST_PROBE_EXPECT_ERROR=${CASE_ERROR}"
			"-DTEST_PROBE_OPTIONS=${case_probe_options}"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/run_package_probe.cmake"
	)
	configure_riwo_ctest_environment(riwo.cmake.package.${name})

	set_tests_properties(riwo.cmake.package.${name} PROPERTIES
		LABELS "cmake;package"
		TIMEOUT 120
	)
endfunction()

# Exercise the complete module dependency matrix.  The expected result is
# calculated independently from the validation code in cmake/options.cmake.
foreach(coro OFF ON)
	foreach(http OFF ON)
		foreach(websocket OFF ON)
			foreach(utils OFF ON)
				set(case_name
					"modules-c${coro}-h${http}-w${websocket}-u${utils}"
				)
				string(TOLOWER "${case_name}" case_name)

				set(case_options
					"RIWO_BUILD_CORO=${coro}"
					"RIWO_BUILD_HTTP=${http}"
					"RIWO_BUILD_WEBSOCKET=${websocket}"
					"RIWO_BUILD_UTILITIES=${utils}"
				)
				if (http AND NOT coro)
					add_riwo_configure_case(${case_name}
						ERROR "<HTTP> module depends on <Coroutine> module"
						OPTIONS ${case_options}
					)
				elseif (websocket AND NOT http)
					add_riwo_configure_case(${case_name}
						ERROR "<WebSocket> module depends on <HTTP> module"
						OPTIONS ${case_options}
					)
				elseif (utils AND NOT coro)
					add_riwo_configure_case(${case_name}
						ERROR "<Utilities> module depends on <Coroutine> module"
						OPTIONS ${case_options}
					)
				else ()
					set(case_components core)
					if (coro)
						list(APPEND case_components coro)
					endif ()

					if (http)
						list(APPEND case_components http)
					endif ()

					if (websocket)
						list(APPEND case_components websocket)
					endif ()

					if (utils)
						list(APPEND case_components utils)
					endif ()

					add_riwo_configure_case(${case_name} SUCCESS
						OPTIONS ${case_options}
						COMPONENTS ${case_components}
					)
				endif ()
			endforeach()
		endforeach()
	endforeach()
endforeach()

add_riwo_configure_case(invalid-asio-provider
	ERROR "Invalid RIWO_ASIO_PROVIDER='INVALID'"
	OPTIONS "RIWO_ASIO_PROVIDER=invalid"
)
add_riwo_configure_case(external-header-providers SUCCESS
	OPTIONS
		"RIWO_ASIO_PROVIDER=external"
		"RIWO_ASIO_INSTALL_PREFIX=${PROJECT_SOURCE_DIR}/3rd_party/asio"
		"RIWO_USE_BUNDLED_SPDLOG=OFF"
		"RIWO_SPDLOG_INSTALL_PREFIX=${PROJECT_SOURCE_DIR}/3rd_party/spdlog"
	COMPONENTS core coro
)
add_riwo_configure_case(utils-udp-default-without-udp
	ERROR "Unsupported Utilities.SoftBus interface: udp"
	OPTIONS
		"RIWO_BUILD_UTILITIES=ON"
		"RIWO_BUILD_UTILITIES_SBUS_UDP=OFF"
		"RIWO_UTILS_SBUS_DEFAULT_INTERFACE=udp"
)
add_riwo_configure_case(stress-without-testing
	ERROR "Stress and performance tests require BUILD_TESTING=ON"
	OPTIONS "RIWO_BUILD_STRESS_TESTS=ON"
)
add_riwo_configure_case(performance-without-testing
	ERROR "Stress and performance tests require BUILD_TESTING=ON"
	OPTIONS "RIWO_BUILD_PERFORMANCE_TESTS=ON"
)
add_riwo_configure_case(cmake-tests-without-testing
	ERROR "CMake integration tests require BUILD_TESTING=ON"
	OPTIONS "RIWO_BUILD_CMAKE_TESTS=ON"
)
add_riwo_configure_case(sanitizers-mutually-exclusive
	ERROR "ASan/UBSan and TSan cannot be enabled together"
	OPTIONS
		"RIWO_ENABLE_TEST_SANITIZERS=ON"
		"RIWO_ENABLE_TEST_TSAN=ON"
)
add_riwo_configure_case(sanitizer-without-testing
	ERROR "Test sanitizers require BUILD_TESTING=ON"
	OPTIONS "RIWO_ENABLE_TEST_SANITIZERS=ON"
)
if (MSVC)
	add_riwo_configure_case(msvc-tsan-unavailable
		ERROR "MSVC does not provide ThreadSanitizer"
		OPTIONS
			"BUILD_TESTING=ON"
			"RIWO_ENABLE_TEST_TSAN=ON"
	)
endif ()

add_riwo_configure_case(performance-with-sanitizer
	ERROR "Performance tests cannot be combined with test sanitizers"
	OPTIONS
		"BUILD_TESTING=ON"
		"RIWO_BUILD_PERFORMANCE_TESTS=ON"
		"RIWO_ENABLE_TEST_SANITIZERS=ON"
)
add_riwo_configure_case(fuzzer-without-testing
	ERROR "Fuzzers require BUILD_TESTING=ON"
	OPTIONS "RIWO_BUILD_FUZZERS=ON"
)
add_riwo_configure_case(invalid-positive-integer
	ERROR "RIWO_FUNCTIONAL_REPEAT must be a positive integer"
	OPTIONS "RIWO_FUNCTIONAL_REPEAT=0"
)
add_riwo_configure_case(invalid-seed
	ERROR "RIWO_FUNCTIONAL_SEED must be a non-negative integer"
	OPTIONS "RIWO_FUNCTIONAL_SEED=-1"
)

set(riwo_unavailable_dependency_probe_options
	"CMAKE_DISABLE_FIND_PACKAGE_spdlog=TRUE"
	"CMAKE_FIND_ROOT_PATH=${riwo_cmake_test_work_dir}/unavailable-root"
	"CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY"
)
set(riwo_external_spdlog_options
	"RIWO_USE_BUNDLED_SPDLOG=OFF"
	"RIWO_SPDLOG_INSTALL_PREFIX=${PROJECT_SOURCE_DIR}/3rd_party/spdlog"
)

add_riwo_package_probe_case(core-ignores-unused-spdlog FOUND
	COMPONENT core
	OPTIONS
		${riwo_external_spdlog_options}
		"RIWO_BUILD_UTILITIES=OFF"
	COMPONENTS core coro
	PROBE_OPTIONS ${riwo_unavailable_dependency_probe_options}
)
add_riwo_package_probe_case(missing-component-precedes-dependencies
	COMPONENT utils
	ERROR "Required Riwo components are unavailable: utils"
	OPTIONS
		${riwo_external_spdlog_options}
		"RIWO_BUILD_UTILITIES=OFF"
	COMPONENTS core coro
	PROBE_OPTIONS ${riwo_unavailable_dependency_probe_options}
)
add_riwo_package_probe_case(utils-restores-external-spdlog
	COMPONENT utils
	ERROR "external spdlog"
	OPTIONS
		${riwo_external_spdlog_options}
		"RIWO_BUILD_UTILITIES=ON"
	COMPONENTS core coro utils
	PROBE_OPTIONS ${riwo_unavailable_dependency_probe_options}
)

set(riwo_websocket_zlib_options
	"RIWO_BUILD_STATIC=ON"
	"RIWO_BUILD_HTTP=ON"
	"RIWO_BUILD_WEBSOCKET=ON"
	"RIWO_HTTP_ZLIB_SUPPORT=OFF"
	"RIWO_WEBSOCKET_ZLIB_SUPPORT=ON"
	"ZLIB_INCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/zlib"
	"ZLIB_LIBRARY=${CMAKE_COMMAND}"
)
add_riwo_package_probe_case(http-ignores-websocket-zlib FOUND
	COMPONENT http
	OPTIONS ${riwo_websocket_zlib_options}
	COMPONENTS core coro http websocket
	PROBE_OPTIONS "CMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE"
)
add_riwo_package_probe_case(websocket-restores-zlib
	COMPONENT websocket
	ERROR "dependency ZLIB"
	OPTIONS ${riwo_websocket_zlib_options}
	COMPONENTS core coro http websocket
	PROBE_OPTIONS "CMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE"
)

set(riwo_consumer_components core)

foreach(component coro http websocket utils)
	string(TOUPPER "${component}" component_upper)

	if (component STREQUAL "utils")
		set(component_enabled ${RIWO_BUILD_UTILITIES})
	else ()
		set(component_enabled ${RIWO_BUILD_${component_upper}})
	endif ()

	if (component_enabled)
		list(APPEND riwo_consumer_components ${component})
	endif ()

endforeach()

string(JOIN "," riwo_consumer_components_arg ${riwo_consumer_components})

set(riwo_missing_component unknown)

foreach(component coro http websocket utils)
	if (NOT component IN_LIST riwo_consumer_components)
		set(riwo_missing_component ${component})
		break()
	endif ()
endforeach()

if (CMAKE_CONFIGURATION_TYPES)
	set(riwo_consumer_configuration "$<CONFIG>")
else ()
	set(riwo_consumer_configuration "${CMAKE_BUILD_TYPE}")
endif ()

# A fuzzer build adds private sanitizer instrumentation directly to the Riwo
# archives.  It is a dedicated harness configuration rather than an installable
# package configuration, so validate installation in all other test builds.
if (NOT RIWO_BUILD_FUZZERS)
	# Use the common launcher so sanitizer process settings (notably TSan's
	# setarch -R personality) are inherited by the nested consumer CTest.
	add_riwo_ctest(riwo.cmake.install-consumer
		${CMAKE_COMMAND}
		"-DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
		"-DTEST_BINARY_DIR=${riwo_cmake_test_work_dir}/consumer-build"
		"-DTEST_MISSING_BINARY_DIR=${riwo_cmake_test_work_dir}/consumer-missing-build"
		"-DTEST_INSTALL_PREFIX=${riwo_cmake_test_work_dir}/install"
		"-DTEST_INSTALL_CMAKEDIR=${RIWO_INSTALL_CMAKEDIR}"
		"-DRIWO_BINARY_DIR=${PROJECT_BINARY_DIR}"
		"-DTEST_GENERATOR=${CMAKE_GENERATOR}"
		"-DTEST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
		"-DTEST_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
		"-DTEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
		"-DTEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
		"-DTEST_RC_COMPILER=${CMAKE_RC_COMPILER}"
		"-DTEST_MT=${CMAKE_MT}"
		"-DTEST_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
		"-DTEST_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
		"-DTEST_CONFIGURATION=${riwo_consumer_configuration}"
		"-DTEST_VERSION=${PROJECT_VERSION}"
		"-DTEST_COMPONENTS=${riwo_consumer_components_arg}"
		"-DTEST_MISSING_COMPONENT=${riwo_missing_component}"
		"-DTEST_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/run_install_consumer.cmake"
	)
	configure_riwo_ctest_environment(riwo.cmake.install-consumer)

	set_tests_properties(riwo.cmake.install-consumer PROPERTIES
		LABELS "cmake;install;consumer"
		RUN_SERIAL TRUE
		TIMEOUT 300
	)
endif ()
