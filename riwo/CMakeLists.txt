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

if (RIWO_BUILD_HTTP_EXP)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES http_exp.h)
	add_subdirectory(http_exp)
endif ()

if (RIWO_BUILD_WEBSOCKET)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES websocket.h)
	add_subdirectory(websocket)
endif ()

if (RIWO_BUILD_WEBSOCKET_EXP)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES websocket_exp.h)
	add_subdirectory(websocket_exp)
endif ()

if (RIWO_BUILD_UTILITIES)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES utils.h)
	add_subdirectory(utils)
endif ()

if (RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN)
	foreach(target riwo.core riwo.coro riwo.http riwo.http_exp riwo.websocket riwo.websocket_exp riwo.utils)
		if (TARGET ${target})
			target_link_libraries(${target} PUBLIC riwo.test.sanitizer)
		endif ()
	endforeach()
endif ()
