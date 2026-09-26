# SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

option(RIWO_BUILD_STATIC
	"-- ${PRO_NAME}: Build static libraries." ${riwo_build_static_default}
)
if (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
	NOT riwo_gnu_shared_runtime_available AND NOT RIWO_BUILD_STATIC)
	message(FATAL_ERROR
		"${PRO_NAME}: Shared libraries require a shared GNU C++ runtime on Windows. "
		"Use -DRIWO_BUILD_STATIC=ON or a MinGW toolchain that provides libstdc++-6.dll."
	)
endif ()

set(RIWO_OPENSSL_INSTALL_PREFIX "" CACHE PATH
	"Install prefix of an external OpenSSL package."
)
set(RIWO_ZLIB_INSTALL_PREFIX "" CACHE PATH
	"Install prefix of an external zlib package."
)
option(RIWO_OPENSSL_SUPPORT
	"-- ${PRO_NAME}: OpenSSL support." OFF
)
if (RIWO_OPENSSL_SUPPORT)
	message(STATUS "${PRO_NAME}: Enable OpenSSL support.")
	add_definitions(-DRIWO_OPENSSL_SUPPORT=1)
endif ()

option(RIWO_IO_URING_SUPPORT
	"-- ${PRO_NAME}: Linux I/O uring support." OFF
)
if (RIWO_IO_URING_SUPPORT)
	if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
		message(FATAL_ERROR
			"${PRO_NAME}: RIWO_IO_URING_SUPPORT requires Linux."
		)
	endif ()
	message(STATUS "${PRO_NAME}: Enable Linux I/O uring support.")
endif ()

option(RIWO_BUILD_CORO
	"-- ${PRO_NAME}: Build module <Coroutine>." ON
)
set(RIWO_CORO_SUPPORT ${RIWO_BUILD_CORO})

if (RIWO_BUILD_CORO)
	message(STATUS "${PRO_NAME}: Build module <Coroutine>.")
endif ()

option(RIWO_BUILD_HTTP
	"-- ${PRO_NAME}: Build module <HTTP>." OFF
)
set(RIWO_HTTP_SUPPORT ${RIWO_BUILD_HTTP})

if (RIWO_BUILD_HTTP)
	if (NOT RIWO_BUILD_CORO)
		message(FATAL_ERROR "${PRO_NAME}: <HTTP> module depends on <Coroutine> module.")
		unset(RIWO_BUILD_HTTP)
	elseif (RIWO_OPENSSL_SUPPORT)
		message(STATUS "${PRO_NAME}: Build module <HTTP/HTTPS>.")
	else ()
		message(STATUS "${PRO_NAME}: Build module <HTTP>.")
	endif ()
endif ()

option(RIWO_BUILD_WEBSOCKET
	"-- ${PRO_NAME}: Build module <WebSocket>." OFF
)
set(RIWO_WEBSOCKET_SUPPORT ${RIWO_BUILD_WEBSOCKET})

if (RIWO_BUILD_WEBSOCKET)
	if (NOT RIWO_BUILD_HTTP)
		message(FATAL_ERROR "${PRO_NAME}: <WebSocket> module depends on <HTTP> module.")
		unset(RIWO_BUILD_WEBSOCKET)
	else ()
		message(STATUS "${PRO_NAME}: Build module <WebSocket>.")
	endif ()
endif ()

option(RIWO_BUILD_UTILITIES
	"-- ${PRO_NAME}: Build module <Utilities>." OFF
)
set(RIWO_UTILITIES_SUPPORT ${RIWO_BUILD_UTILITIES})

if (RIWO_BUILD_UTILITIES)
	if (NOT RIWO_BUILD_CORO)
		message(FATAL_ERROR "${PRO_NAME}: <Utilities> module depends on <Coroutine> module.")
		unset(RIWO_BUILD_UTILITIES)
	else ()
		message(STATUS "${PRO_NAME}: Build module <Utilities>.")
	endif ()
endif ()

option(RIWO_BUILD_EXAMPLES
	"-- ${PRO_NAME}: Enable this to build the examples." OFF
)
if (RIWO_BUILD_EXAMPLES)
	message(STATUS "${PRO_NAME}: Enable this to build the examples.")
endif()

set(RIWO_CONFIG_INCLUDE
	${RIWO_OUTPUT_DIR}/config_include
)
set(RIWO_CONFIG_INCLUDE
	${RIWO_CONFIG_INCLUDE} CACHE PATH
	"Path to riwo config include directory."
)
configure_file (
	${PROJECT_SOURCE_DIR}/riwo/core/cxx/configs.h.in
	${RIWO_CONFIG_INCLUDE}/riwo/core/cxx/configs.h
	@ONLY
)
include_directories(${RIWO_CONFIG_INCLUDE})

install(FILES
	${RIWO_CONFIG_INCLUDE}/riwo/core/cxx/configs.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/core/cxx
)
