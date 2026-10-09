# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

add_riwo_test(core core/basic.cpp riwo.core)
add_riwo_test(core.facilities core/facilities.cpp riwo.core)
add_riwo_test(core.runtime core/runtime.cpp riwo.core)
add_riwo_test(core.jthread core/jthread.cpp riwo.core)

# The native standard-library implementation is trusted. Force the fallback so
# this target always exercises Riwo's compatibility implementation.
target_compile_definitions(riwo.test.core.jthread PRIVATE
	RIWO_HAS_STD_JTHREAD=0
)
add_library(riwo.test.core.fixture SHARED core/library_fixture.cpp)
riwo_enable_strict_warnings(riwo.test.core.fixture)

set_target_properties(riwo.test.core.fixture PROPERTIES
	PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY ${RIWO_OUTPUT_DIR}/bin
	LIBRARY_OUTPUT_DIRECTORY ${RIWO_OUTPUT_DIR}/bin
)
add_riwo_test(core.public-api core/public_api.cpp riwo.core
	$<TARGET_FILE:riwo.test.core.fixture>
)
add_dependencies(riwo.test.core.public-api riwo.test.core.fixture)

if (RIWO_BUILD_CORO)
	add_riwo_test(coro coro/basic.cpp riwo.coro)
endif ()

if (RIWO_BUILD_HTTP)
	add_riwo_test(http http/basic.cpp riwo.http)
	add_riwo_test(http.protocol http/protocol.cpp riwo.http)

	add_riwo_test(http.public-api http/public_api.cpp riwo.http)
	add_riwo_test(http.loopback http/loopback.cpp riwo.http)
endif ()

if (RIWO_BUILD_WEBSOCKET)
	add_riwo_test(websocket.protocol websocket/protocol.cpp riwo.websocket)
	add_riwo_test(websocket.public-api websocket/public_api.cpp riwo.websocket)

	add_riwo_test(websocket.handshake websocket/handshake.cpp riwo.websocket)
	add_riwo_test(websocket.client-server websocket/client_server.cpp riwo.websocket)

	if (RIWO_OPENSSL_SUPPORT)
		add_riwo_test(websocket.wss websocket/wss.cpp riwo.websocket)
	endif ()

	add_riwo_test(websocket.stream websocket/stream.cpp riwo.websocket)
endif ()

if (RIWO_BUILD_UTILITIES)
	add_riwo_test(utils utils/basic.cpp riwo.utils)
	add_riwo_test(utils.runtime utils/runtime.cpp riwo.utils)
	add_riwo_test(utils.sbus utils/sbus.cpp riwo.utils)
	add_riwo_test(utils.sbus.cache utils/sbus_cache.cpp riwo.utils)
	add_riwo_test(utils.modules utils/modules.cpp riwo.utils)
endif ()


# Network loopback tests remain the dependency-free baseline. When an
# independent implementation is already available in the build environment,
# add interoperability checks without downloading anything at configure time.
find_package(Python3 3.8 QUIET COMPONENTS Interpreter)

if (Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
	if (RIWO_BUILD_HTTP)
		find_program(RIWO_TEST_CURL_EXECUTABLE NAMES curl)

		if (RIWO_TEST_CURL_EXECUTABLE)
			set(riwo_http_interop_backend curl)
			set(riwo_http_interop_arguments
				--curl ${RIWO_TEST_CURL_EXECUTABLE}
			)
		else ()
			set(riwo_http_interop_backend python-stdlib)
			set(riwo_http_interop_arguments)
		endif ()

		add_riwo_test_program(riwo.test.http.interop-peer
			http/interop_peer.cpp riwo.http
		)
		add_riwo_ctest(riwo.http.interop
			${Python3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/interop/http_interop.py
			--peer $<TARGET_FILE:riwo.test.http.interop-peer>
			--backend ${riwo_http_interop_backend}
			${riwo_http_interop_arguments}
		)
		configure_riwo_functional_test(riwo.http.interop)

		set_property(TEST riwo.http.interop APPEND PROPERTY LABELS interop external)

		message(STATUS
			"${PRO_NAME}: HTTP interoperability backend: ${riwo_http_interop_backend}"
		)
	endif ()

	if (RIWO_BUILD_WEBSOCKET)
		find_program(RIWO_TEST_NODE_EXECUTABLE NAMES node nodejs)

		set(riwo_websocket_interop_backend "")
		set(riwo_websocket_interop_arguments)

		if (RIWO_TEST_NODE_EXECUTABLE)
			execute_process (
				COMMAND ${RIWO_TEST_NODE_EXECUTABLE} -p "require.resolve('ws')"
				RESULT_VARIABLE riwo_node_ws_result
				OUTPUT_VARIABLE riwo_node_ws_module
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET
			)
			if (riwo_node_ws_result EQUAL 0)
				set(riwo_websocket_interop_backend node-ws)
				set(riwo_websocket_interop_arguments
					--node ${RIWO_TEST_NODE_EXECUTABLE}
					--node-ws-module ${riwo_node_ws_module}
					--node-helper
					${CMAKE_CURRENT_SOURCE_DIR}/interop/websocket_node_ws.js
				)
			endif ()
		endif ()

		if (NOT riwo_websocket_interop_backend)
			execute_process (
				COMMAND ${Python3_EXECUTABLE} -c
					"import websockets; assert hasattr(websockets, 'connect')"
				RESULT_VARIABLE riwo_python_websockets_result
				OUTPUT_QUIET ERROR_QUIET
			)
			if (riwo_python_websockets_result EQUAL 0)
				set(riwo_websocket_interop_backend python-websockets)
			endif ()
		endif ()

		if (NOT riwo_websocket_interop_backend)
			execute_process (
				COMMAND ${Python3_EXECUTABLE} -c
					"import websocket; assert hasattr(websocket, 'create_connection')"
				RESULT_VARIABLE riwo_python_websocket_client_result
				OUTPUT_QUIET ERROR_QUIET
			)
			if (riwo_python_websocket_client_result EQUAL 0)
				set(riwo_websocket_interop_backend python-websocket-client)
			endif ()
		endif ()

		if (NOT riwo_websocket_interop_backend)
			find_program(RIWO_TEST_WSCAT_EXECUTABLE NAMES wscat)

			if (RIWO_TEST_WSCAT_EXECUTABLE)
				set(riwo_websocket_interop_backend wscat)
				set(riwo_websocket_interop_arguments
					--wscat ${RIWO_TEST_WSCAT_EXECUTABLE}
				)
			endif ()
		endif ()

		if (riwo_websocket_interop_backend)
			add_riwo_test_program(riwo.test.websocket.interop-peer
				websocket/interop_peer.cpp riwo.websocket
			)
			add_riwo_ctest(riwo.websocket.interop
				${Python3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/interop/websocket_interop.py
				--peer $<TARGET_FILE:riwo.test.websocket.interop-peer>
				--backend ${riwo_websocket_interop_backend}
				${riwo_websocket_interop_arguments}
			)
			configure_riwo_functional_test(riwo.websocket.interop)

			set_property(TEST riwo.websocket.interop APPEND PROPERTY
				LABELS interop external
			)
			message(STATUS
				"${PRO_NAME}: WebSocket interoperability backend: ${riwo_websocket_interop_backend}"
			)
		else ()
			message(STATUS
				"${PRO_NAME}: No external WebSocket client found; using Riwo loopback tests."
			)
		endif ()
	endif ()
else ()
	if (CMAKE_CROSSCOMPILING)
		message(STATUS
			"${PRO_NAME}: External interoperability tests disabled while cross-compiling; using Riwo loopback tests."
		)
	else ()
		message(STATUS
			"${PRO_NAME}: Python 3.8+ not found; using Riwo HTTP/WebSocket loopback tests."
		)
	endif ()
endif ()
