# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

include(CMakeParseArguments)

if(CMAKE_COMPILER_IS_GNUCXX)
	include(CheckCXXCompilerFlag)
	check_cxx_compiler_flag("-Wa,-mbig-obj" RIWO_EXAMPLE_GNU_BIG_OBJ)
endif()


function(riwo_add_example category name)
	set(options)
	set(one_value_args)
	set(multi_value_args SOURCES LIBRARIES)

	cmake_parse_arguments(EXAMPLE
		"${options}" "${one_value_args}" "${multi_value_args}" ${ARGN}
	)
	if(NOT EXAMPLE_SOURCES)
		set(EXAMPLE_SOURCES "${category}/${name}.cpp")
	endif()

	set(target "riwo.example.${category}.${name}")
	add_executable(${target} ${EXAMPLE_SOURCES})

	if (category MATCHES "^(http|websocket)$")
		riwo_limit_heavy_compile(${target})
	endif ()

	target_link_libraries(${target} PRIVATE ${EXAMPLE_LIBRARIES})
	target_compile_options(${target} PRIVATE
		$<$<CXX_COMPILER_ID:MSVC>:/bigobj>
		$<$<AND:$<CXX_COMPILER_ID:GNU>,$<BOOL:${RIWO_EXAMPLE_GNU_BIG_OBJ}>>:-Wa,-mbig-obj>
	)
	set_target_properties(${target} PROPERTIES
		OUTPUT_NAME ${name}
		RUNTIME_OUTPUT_DIRECTORY "${RIWO_OUTPUT_DIR}/examples/${category}"
	)
	install(TARGETS ${target}
		RUNTIME DESTINATION "examples/${category}"
	)
endfunction()


# Core
riwo_add_example(core algorithms      LIBRARIES riwo.core)
riwo_add_example(core app_paths       LIBRARIES riwo.core)
riwo_add_example(core args_parser     LIBRARIES riwo.core)
riwo_add_example(core execution       LIBRARIES riwo.core)
riwo_add_example(core ini             LIBRARIES riwo.core)
riwo_add_example(core lock_free_queue LIBRARIES riwo.core)
riwo_add_example(core value           LIBRARIES riwo.core)

add_library(riwo.example.core.dynamic_library_plugin SHARED
	core/dynamic_library_plugin.cpp
)
set_target_properties(riwo.example.core.dynamic_library_plugin PROPERTIES
	OUTPUT_NAME dynamic_library_plugin
	PREFIX ""
	LIBRARY_OUTPUT_DIRECTORY "${RIWO_OUTPUT_DIR}/examples/core"
	RUNTIME_OUTPUT_DIRECTORY "${RIWO_OUTPUT_DIR}/examples/core"
)
install(TARGETS riwo.example.core.dynamic_library_plugin
	LIBRARY DESTINATION examples/core
	RUNTIME DESTINATION examples/core
)
riwo_add_example(core dynamic_library LIBRARIES riwo.core)
add_dependencies(riwo.example.core.dynamic_library
	riwo.example.core.dynamic_library_plugin
)

# Coroutines
if (RIWO_BUILD_CORO)
	riwo_add_example(coro basics             LIBRARIES riwo.coro)
	riwo_add_example(coro mutex              LIBRARIES riwo.coro)
	riwo_add_example(coro shared_mutex       LIBRARIES riwo.coro)
	riwo_add_example(coro semaphore          LIBRARIES riwo.coro)
	riwo_add_example(coro condition_variable LIBRARIES riwo.coro)
endif ()

# HTTP/1.0 and HTTP/1.1
if (RIWO_BUILD_HTTP)
	riwo_add_example(http client_sync      LIBRARIES riwo.http)
	riwo_add_example(http client_awaitable LIBRARIES riwo.http)
	riwo_add_example(http client_cookies   LIBRARIES riwo.http)
	riwo_add_example(http client_file      LIBRARIES riwo.http)
	riwo_add_example(http proxy_client     LIBRARIES riwo.http)
	riwo_add_example(http protocol         LIBRARIES riwo.http)
	riwo_add_example(http server           LIBRARIES riwo.http)
	riwo_add_example(http server_aop       LIBRARIES riwo.http)
	riwo_add_example(http server_file      LIBRARIES riwo.http)
	riwo_add_example(http server_session   LIBRARIES riwo.http)

	if (RIWO_OPENSSL_SUPPORT)
		riwo_add_example(http https_server LIBRARIES riwo.http)
	endif ()
endif ()

# WebSocket protocol
if (RIWO_BUILD_WEBSOCKET)
	riwo_add_example(websocket client            LIBRARIES riwo.websocket)
	riwo_add_example(websocket proxy_client      LIBRARIES riwo.websocket)
	riwo_add_example(websocket retry_open        LIBRARIES riwo.websocket)
	riwo_add_example(websocket server            LIBRARIES riwo.websocket)
	riwo_add_example(websocket protocol          LIBRARIES riwo.websocket)
	riwo_add_example(websocket mixed_http_client LIBRARIES riwo.websocket)
	riwo_add_example(websocket mixed_http_server LIBRARIES riwo.websocket)

	if (RIWO_OPENSSL_SUPPORT)
		riwo_add_example(websocket wss_client LIBRARIES riwo.websocket)
		riwo_add_example(websocket wss_server LIBRARIES riwo.websocket)
	endif ()
endif ()

# Application utilities
if (RIWO_BUILD_UTILITIES)
	riwo_add_example(utils logger             LIBRARIES riwo.utils)
	riwo_add_example(utils observer           LIBRARIES riwo.utils)
	riwo_add_example(utils process            LIBRARIES riwo.utils)
	riwo_add_example(utils soft_bus_local     LIBRARIES riwo.utils)
	riwo_add_example(utils soft_bus_transport LIBRARIES riwo.utils)
	if (RIWO_BUILD_UTILITIES_SBUS_UDP)
		riwo_add_example(utils soft_bus_udp LIBRARIES riwo.utils)
	endif ()
	riwo_add_example(utils settings           LIBRARIES riwo.utils)
	riwo_add_example(utils signal_slot        LIBRARIES riwo.utils)
	riwo_add_example(utils modules
		SOURCES
			utils/modules/main.cpp
			utils/modules/foundation.cpp
			utils/modules/cache.cpp
			utils/modules/api.cpp
		LIBRARIES riwo.utils
	)
endif ()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	core/algorithms.cpp
	core/app_paths.cpp
	core/args_parser.cpp
	core/dynamic_library.cpp
	core/dynamic_library_plugin.cpp
	core/execution.cpp
	core/ini.cpp
	core/lock_free_queue.cpp
	core/value.cpp
	coro/basics.cpp
	coro/condition_variable.cpp
	coro/mutex.cpp
	coro/semaphore.cpp
	coro/shared_mutex.cpp
	http/client_awaitable.cpp
	http/client_cookies.cpp
	http/client_file.cpp
	http/client_sync.cpp
	http/https_server.cpp
	http/protocol.cpp
	http/proxy_client.cpp
	http/server.cpp
	http/server_aop.cpp
	http/server_file.cpp
	http/server_session.cpp
	websocket/mixed_http_client.cpp
	websocket/mixed_http_server.cpp
	websocket/protocol.cpp
	websocket/client.cpp
	websocket/proxy_client.cpp
	websocket/retry_open.cpp
	websocket/server.cpp
	websocket/wss_client.cpp
	websocket/wss_server.cpp
	utils/logger.cpp
	utils/modules/api.cpp
	utils/modules/cache.cpp
	utils/modules/foundation.cpp
	utils/modules/main.cpp
	utils/observer.cpp
	utils/process.cpp
	utils/soft_bus_local.cpp
	utils/soft_bus_transport.cpp
	utils/soft_bus_udp.cpp
	utils/settings.cpp
	utils/signal_slot.cpp
)
