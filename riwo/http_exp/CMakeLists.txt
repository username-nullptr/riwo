# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

riwo_configure_http_exp_nghttp2()

message(STATUS
	"${PRO_NAME}: HTTP_EXP HTTP/2 support: ${RIWO_HTTP_EXP_HTTP2_SUPPORT}; "
	"nghttp2 provider=${RIWO_HTTP_EXP_NGHTTP2_PROVIDER}, "
	"version=${RIWO_HTTP_EXP_NGHTTP2_VERSION}, "
	"linkage=${RIWO_HTTP_EXP_NGHTTP2_LINKAGE}, "
	"api=${RIWO_HTTP_EXP_NGHTTP2_API}."
)
if (RIWO_HTTP_EXP_HTTP2_SUPPORT AND NOT RIWO_OPENSSL_SUPPORT)
	message(STATUS
		"${PRO_NAME}: HTTP_EXP HTTP/2 is limited to cleartext prior knowledge "
		"because OpenSSL support is disabled."
	)
endif ()

set(target_name riwo.http_exp)
add_project(${target_name} riwo.coro)
target_compile_definitions(${target_name} PRIVATE RIWO_HTTP_EXP_SHARED)

# Keep the protocol layer usable as a pure byte-in/byte-out engine.  Session
# drivers are the composition boundary and may depend on both protocol and
# transport, but dependencies in the opposite direction are architectural
# errors even when the affected header is private.
file(GLOB_RECURSE riwo_http_exp_protocol_sources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.h"
)
foreach(source IN LISTS riwo_http_exp_protocol_sources)
	file(STRINGS "${source}" forbidden_layer_includes REGEX
		"^[ \t]*#[ \t]*include[ \t]*[<\"]riwo/http_exp/(client|server|session|transport)/"
	)
	if (forbidden_layer_includes)
		message(FATAL_ERROR
			"${PRO_NAME}: HTTP_EXP protocol layer has a forbidden higher/I/O-layer include in "
			"${source}: ${forbidden_layer_includes}"
		)
	endif ()
endforeach()

unset(forbidden_layer_includes)
unset(riwo_http_exp_protocol_sources)

option(RIWO_HTTP_EXP_ZLIB_SUPPORT
	"-- ${PRO_NAME}: HTTP_EXP z-lib support." OFF
)
if (RIWO_HTTP_EXP_ZLIB_SUPPORT)
	message(STATUS "${PRO_NAME}: Enable HTTP_EXP z-lib support.")
	target_compile_definitions(${target_name} PUBLIC -DRIWO_HTTP_EXP_ZLIB_SUPPORT=1)

	if (RIWO_ZLIB_INSTALL_PREFIX)
		set(ZLIB_ROOT "${RIWO_ZLIB_INSTALL_PREFIX}")
	endif ()

	find_package(ZLIB REQUIRED)
	riwo_validate_msvc_imported_targets(ZLIB
		RIWO_ZLIB_INSTALL_PREFIX ZLIB::ZLIB
	)
	target_link_libraries(${target_name} PRIVATE ZLIB::ZLIB)
endif ()

if (RIWO_HTTP_EXP_HTTP2_SUPPORT)
	target_link_libraries(${target_name} PRIVATE Riwo::http_exp_nghttp2)
endif ()

set(RIWO_HTTP_EXP_CONFIG_INCLUDE ${RIWO_OUTPUT_DIR}/config_include)

configure_file (
	${PROJECT_SOURCE_DIR}/riwo/http_exp/cxx/configs.h.in
	${RIWO_HTTP_EXP_CONFIG_INCLUDE}/riwo/http_exp/cxx/configs.h
	@ONLY
)
install(FILES
	${RIWO_HTTP_EXP_CONFIG_INCLUDE}/riwo/http_exp/cxx/configs.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/http_exp/cxx
)
set(RIWO_HTTP_EXP_ZLIB_SUPPORT ${RIWO_HTTP_EXP_ZLIB_SUPPORT} PARENT_SCOPE)
set(RIWO_HTTP_EXP_HTTP2_SUPPORT ${RIWO_HTTP_EXP_HTTP2_SUPPORT} PARENT_SCOPE)

set(RIWO_HTTP_EXP_NGHTTP2_PROVIDER "${RIWO_HTTP_EXP_NGHTTP2_PROVIDER}" PARENT_SCOPE)
set(RIWO_HTTP_EXP_NGHTTP2_VERSION "${RIWO_HTTP_EXP_NGHTTP2_VERSION}" PARENT_SCOPE)
set(RIWO_HTTP_EXP_NGHTTP2_LINKAGE "${RIWO_HTTP_EXP_NGHTTP2_LINKAGE}" PARENT_SCOPE)
