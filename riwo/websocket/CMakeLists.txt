# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(target_name riwo.websocket)
add_project(${target_name} riwo.http)

target_compile_definitions (
	${target_name} PRIVATE RIWO_WEBSOCKET_SHARED
)
if (RIWO_HTTP_ZLIB_SUPPORT)
	set(RIWO_WEBSOCKET_ZLIB_SUPPORT ON)
	message(STATUS
		"${PRO_NAME}: WebSocket inherits z-lib support from the HTTP module."
	)
endif ()

option(RIWO_WEBSOCKET_ZLIB_SUPPORT
	"-- ${PRO_NAME}: WebSocket permessage-deflate support." OFF
)
if (RIWO_WEBSOCKET_ZLIB_SUPPORT)
	message(STATUS "${PRO_NAME}: Enable WebSocket permessage-deflate support.")

	if (RIWO_ZLIB_INSTALL_PREFIX)
		set(ZLIB_ROOT "${RIWO_ZLIB_INSTALL_PREFIX}")
	endif ()
	find_package(ZLIB REQUIRED)

	riwo_validate_msvc_imported_targets(ZLIB
		RIWO_ZLIB_INSTALL_PREFIX ZLIB::ZLIB
	)
	target_compile_definitions(${target_name}
		PUBLIC RIWO_WEBSOCKET_ZLIB_SUPPORT=1
	)
	target_link_libraries(${target_name} PRIVATE ZLIB::ZLIB)
endif ()

set(RIWO_WEBSOCKET_CONFIG_INCLUDE
	${RIWO_OUTPUT_DIR}/config_include
)
configure_file (
	${PROJECT_SOURCE_DIR}/riwo/websocket/cxx/configs.h.in
	${RIWO_WEBSOCKET_CONFIG_INCLUDE}/riwo/websocket/cxx/configs.h
	@ONLY
)
include_directories(${RIWO_WEBSOCKET_CONFIG_INCLUDE})

install(FILES
	${RIWO_WEBSOCKET_CONFIG_INCLUDE}/riwo/websocket/cxx/configs.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/websocket/cxx
)
if (WIN32)
	target_link_libraries(${target_name} PRIVATE bcrypt)
endif ()
