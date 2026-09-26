# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

include(project)


function(riwo_install_module_headers module)
	install(FILES ${module}.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo)
	install(DIRECTORY ${module}/
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/${module}
		FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.ipp"
		${ARGN}
	)
endfunction()


source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES core.h)
add_subdirectory(core)
# This PIMPL definition is compiled into riwo.core and is not included by any
# public header.
riwo_install_module_headers(core
	PATTERN "library_impl.ipp" EXCLUDE
)

if (RIWO_BUILD_CORO)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES coro.h)
	add_subdirectory(coro)
	riwo_install_module_headers(coro)
endif ()

if (RIWO_BUILD_HTTP)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES http.h)
	add_subdirectory(http)
	# The base parser is an implementation detail used only by the client and
	# server parser translation units. Other detail headers contain public
	# template definitions and must remain in the install tree.
	riwo_install_module_headers(http
		REGEX "/protocol/utils/core/parser[.]h$" EXCLUDE
		REGEX "/protocol/utils/core/detail/parser[.]h$" EXCLUDE
	)
endif ()

if (RIWO_BUILD_WEBSOCKET)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES websocket.h)
	add_subdirectory(websocket)
	riwo_install_module_headers(websocket)
endif ()

if (RIWO_BUILD_UTILITIES)
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES utils.h)
	add_subdirectory(utils)
	riwo_install_module_headers(utils)
endif ()

if (RIWO_ENABLE_TEST_SANITIZERS OR RIWO_ENABLE_TEST_TSAN)
	foreach(target riwo.core riwo.coro riwo.http riwo.websocket riwo.utils)
		if (TARGET ${target})
			target_link_libraries(${target} PUBLIC riwo.test.sanitizer)
		endif ()
	endforeach()
endif ()
