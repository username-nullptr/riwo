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
	# riwo_enable_strict_warnings(${target})

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
riwo_add_example(core executable_paths LIBRARIES riwo.core)
riwo_add_example(core args_parser     LIBRARIES riwo.core)
riwo_add_example(core execution       LIBRARIES riwo.core)
riwo_add_example(core ini             LIBRARIES riwo.core)
riwo_add_example(core lock_free_queue LIBRARIES riwo.core)
riwo_add_example(core value           LIBRARIES riwo.core)

add_library(riwo.example.core.dynamic_library_plugin SHARED
	core/dynamic_library_plugin.cpp
)
riwo_enable_strict_warnings(riwo.example.core.dynamic_library_plugin)
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

# System
if (RIWO_BUILD_SYSTEM)
	riwo_add_example(system directory_watcher LIBRARIES riwo.system)
	riwo_add_example(system environment       LIBRARIES riwo.system)
	riwo_add_example(system files             LIBRARIES riwo.system)
	riwo_add_example(system process           LIBRARIES riwo.system)
	riwo_add_example(system process_capture   LIBRARIES riwo.system)
	riwo_add_example(system process_stdio     LIBRARIES riwo.system)
endif ()

# Coroutines
if (RIWO_BUILD_CORO)
	riwo_add_example(coro basics             LIBRARIES riwo.coro)
	riwo_add_example(coro mutex              LIBRARIES riwo.coro)
	riwo_add_example(coro shared_mutex       LIBRARIES riwo.coro)
	riwo_add_example(coro semaphore          LIBRARIES riwo.coro)
	riwo_add_example(coro condition_variable LIBRARIES riwo.coro)
endif ()

# Crypto
if (RIWO_BUILD_CRYPTO)
	riwo_add_example(crypto algorithms LIBRARIES riwo.crypto)
endif ()

# HTTP
if (RIWO_BUILD_HTTP)
	foreach(name client client_sync client_awaitable client_cookies client_file
		proxy_client protocol server server_acceptor server_aop server_file
		server_session transport)
		riwo_add_example(http ${name}
			SOURCES http/${name}.cpp
			LIBRARIES riwo.http
		)
	endforeach()

	if (RIWO_OPENSSL_SUPPORT)
		riwo_add_example(http https_server
			SOURCES http/https_server.cpp
			LIBRARIES riwo.http
		)
	endif ()
endif ()

# WebSocket
if (RIWO_BUILD_WEBSOCKET)
	riwo_add_example(websocket client
		SOURCES websocket/client.cpp
		LIBRARIES riwo.websocket
	)
	riwo_add_example(websocket server
		SOURCES websocket/server.cpp
		LIBRARIES riwo.websocket
	)
endif ()

# Application utilities
if (RIWO_BUILD_UTILITIES)
	riwo_add_example(utils logger             LIBRARIES riwo.utils)
	riwo_add_example(utils observer           LIBRARIES riwo.utils)
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
	crypto/algorithms.cpp
	core/executable_paths.cpp
	core/args_parser.cpp
	core/dynamic_library.cpp
	core/dynamic_library_plugin.cpp
	core/execution.cpp
	core/ini.cpp
	core/lock_free_queue.cpp
	core/value.cpp
	system/directory_watcher.cpp
	system/environment.cpp
	system/files.cpp
	system/process.cpp
	system/process_capture.cpp
	system/process_stdio.cpp
	coro/basics.cpp
	coro/condition_variable.cpp
	coro/mutex.cpp
	coro/semaphore.cpp
	coro/shared_mutex.cpp
	http/client.cpp
	http/client_awaitable.cpp
	http/client_cookies.cpp
	http/client_file.cpp
	http/client_sync.cpp
	http/common.h
	http/https_server.cpp
	http/protocol.cpp
	http/proxy_client.cpp
	http/server.cpp
	http/server_acceptor.cpp
	http/server_aop.cpp
	http/server_file.cpp
	http/server_session.cpp
	http/transport.cpp
	websocket/client.cpp
	websocket/server.cpp
	utils/logger.cpp
	utils/modules/api.cpp
	utils/modules/cache.cpp
	utils/modules/foundation.cpp
	utils/modules/main.cpp
	utils/observer.cpp
	utils/soft_bus_local.cpp
	utils/soft_bus_transport.cpp
	utils/soft_bus_udp.cpp
	utils/settings.cpp
	utils/signal_slot.cpp
)
