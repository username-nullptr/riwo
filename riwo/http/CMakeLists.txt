# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(target_name riwo.http)
add_project(${target_name} riwo.coro)

target_compile_definitions (
	${target_name} PRIVATE RIWO_HTTP_SHARED
)
option(RIWO_HTTP_ZLIB_SUPPORT
	"-- ${PRO_NAME}: HTTP z-lib support." OFF
)
if (RIWO_HTTP_ZLIB_SUPPORT)
	message(STATUS "${PRO_NAME}: Enable z-lib support.")
	target_compile_definitions(${target_name} PUBLIC -DRIWO_HTTP_ZLIB_SUPPORT=1)

	if (RIWO_ZLIB_INSTALL_PREFIX)
		set(ZLIB_ROOT "${RIWO_ZLIB_INSTALL_PREFIX}")
	endif ()
	find_package(ZLIB REQUIRED)

	riwo_validate_msvc_imported_targets(ZLIB
		RIWO_ZLIB_INSTALL_PREFIX ZLIB::ZLIB
	)
	target_link_libraries(${target_name} PRIVATE ZLIB::ZLIB)
endif ()

set(RIWO_HTTP_CONFIG_INCLUDE
	${RIWO_OUTPUT_DIR}/config_include
)
set(RIWO_HTTP_CONFIG_INCLUDE
	${RIWO_HTTP_CONFIG_INCLUDE} CACHE PATH
	"Path to riwo config include directory."
)
configure_file (
	${PROJECT_SOURCE_DIR}/riwo/http/cxx/configs.h.in
	${RIWO_HTTP_CONFIG_INCLUDE}/riwo/http/cxx/configs.h
	@ONLY
)
include_directories(${RIWO_HTTP_CONFIG_INCLUDE})

install(FILES
	${RIWO_HTTP_CONFIG_INCLUDE}/riwo/http/cxx/configs.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/http/cxx
)
