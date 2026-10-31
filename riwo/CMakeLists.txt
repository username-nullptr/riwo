# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

include(project)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES core.h)
add_subdirectory(core)

if (RIWO_BUILD_CORO)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES coro.h)
	add_subdirectory(coro)
endif ()

if (RIWO_BUILD_HTTP)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES http.h)
	add_subdirectory(http)
endif ()

if (RIWO_BUILD_WEBSOCKET)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES websocket.h)
	add_subdirectory(websocket)
endif ()

if (RIWO_BUILD_UTILITIES)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES utils.h)
	add_subdirectory(utils)
endif ()

add_subdirectory(experimental)

set(TARGET_LIST
	riwo.core
	riwo.coro
	riwo.http
	riwo.websocket
	riwo.utils
	riwo.exp_http
	riwo.exp_websocket
)
if (RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN)
	foreach(target ${TARGET_LIST})
		if (TARGET ${target})
			target_link_libraries(${target} PUBLIC riwo.test.sanitizer)
		endif ()
	endforeach()
endif ()
