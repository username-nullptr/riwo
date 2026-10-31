# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

riwo_configure_exp_http_nghttp2()

message(STATUS
	"${PRO_NAME}: EXP_HTTP HTTP/2 support: ${RIWO_EXP_HTTP_HTTP2_SUPPORT}; "
	"nghttp2 provider=${RIWO_EXP_HTTP_NGHTTP2_PROVIDER}, "
	"version=${RIWO_EXP_HTTP_NGHTTP2_VERSION}, "
	"linkage=${RIWO_EXP_HTTP_NGHTTP2_LINKAGE}, "
	"api=${RIWO_EXP_HTTP_NGHTTP2_API}."
)
if (RIWO_EXP_HTTP_HTTP2_SUPPORT AND NOT RIWO_OPENSSL_SUPPORT)
	message(STATUS
		"${PRO_NAME}: EXP_HTTP HTTP/2 is limited to cleartext prior knowledge "
		"because OpenSSL support is disabled."
	)
endif ()

set(target_name riwo.exp_http)
add_project(${target_name} riwo.core)
target_compile_definitions(${target_name} PRIVATE RIWO_EXP_HTTP_SHARED)

# Keep the protocol layer usable as a pure byte-in/byte-out engine.  Session
# drivers are the composition boundary and may depend on both protocol and
# transport, but dependencies in the opposite direction are architectural
# errors even when the affected header is private.
file(GLOB_RECURSE riwo_exp_http_protocol_sources CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/protocol/*.h"
)
foreach(source IN LISTS riwo_exp_http_protocol_sources)
	file(STRINGS "${source}" forbidden_layer_includes REGEX
		"^[ \t]*#[ \t]*include[ \t]*[<\"]riwo/experimental/http/(body|client|server|session|transport)/"
	)
	if (forbidden_layer_includes)
		message(FATAL_ERROR
			"${PRO_NAME}: EXP_HTTP protocol layer has a forbidden higher/I/O-layer include in "
			"${source}: ${forbidden_layer_includes}"
		)
	endif ()
endforeach()

unset(forbidden_layer_includes)
unset(riwo_exp_http_protocol_sources)

option(RIWO_EXP_HTTP_ZLIB_SUPPORT
	"-- ${PRO_NAME}: EXP_HTTP z-lib support." OFF
)
if (RIWO_EXP_HTTP_ZLIB_SUPPORT)
	message(STATUS "${PRO_NAME}: Enable EXP_HTTP z-lib support.")
	target_compile_definitions(${target_name} PUBLIC -DRIWO_EXP_HTTP_ZLIB_SUPPORT=1)

	if (RIWO_ZLIB_INSTALL_PREFIX)
		set(ZLIB_ROOT "${RIWO_ZLIB_INSTALL_PREFIX}")
	endif ()

	find_package(ZLIB REQUIRED)
	riwo_validate_msvc_imported_targets(ZLIB
		RIWO_ZLIB_INSTALL_PREFIX ZLIB::ZLIB
	)
	target_link_libraries(${target_name} PRIVATE ZLIB::ZLIB)
endif ()

if (RIWO_EXP_HTTP_HTTP2_SUPPORT)
	target_link_libraries(${target_name} PRIVATE Riwo::exp_http_nghttp2)
endif ()

set(RIWO_EXP_HTTP_CONFIG_INCLUDE ${RIWO_OUTPUT_DIR}/config_include)

configure_file (
	${PROJECT_SOURCE_DIR}/riwo/experimental/http/cxx/configs.h.in
	${RIWO_EXP_HTTP_CONFIG_INCLUDE}/riwo/experimental/http/cxx/configs.h
	@ONLY
)
install(FILES
	${RIWO_EXP_HTTP_CONFIG_INCLUDE}/riwo/experimental/http/cxx/configs.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/experimental/http/cxx
)
set(RIWO_EXP_HTTP_ZLIB_SUPPORT ${RIWO_EXP_HTTP_ZLIB_SUPPORT} PARENT_SCOPE)
set(RIWO_EXP_HTTP_HTTP2_SUPPORT ${RIWO_EXP_HTTP_HTTP2_SUPPORT} PARENT_SCOPE)

set(RIWO_EXP_HTTP_NGHTTP2_PROVIDER "${RIWO_EXP_HTTP_NGHTTP2_PROVIDER}" PARENT_SCOPE)
set(RIWO_EXP_HTTP_NGHTTP2_VERSION "${RIWO_EXP_HTTP_NGHTTP2_VERSION}" PARENT_SCOPE)
set(RIWO_EXP_HTTP_NGHTTP2_LINKAGE "${RIWO_EXP_HTTP_NGHTTP2_LINKAGE}" PARENT_SCOPE)

install(FILES ../http.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/experimental
)
install(DIRECTORY ./
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/experimental/http
	FILES_MATCHING
	PATTERN "*.h"
	PATTERN "*.hpp"
	PATTERN "*.ipp"
	PATTERN "v2.md" EXCLUDE
	REGEX "/session/v1/" EXCLUDE
	REGEX "/session/v2/" EXCLUDE
	REGEX "/protocol/detail/" EXCLUDE
	REGEX "/protocol/v1/detail/" EXCLUDE
	REGEX "/protocol/v2/detail/nghttp2_compat[.]h$" EXCLUDE
	REGEX "/client/detail/factory[.]h$" EXCLUDE
	REGEX "/client/detail/request_access[.]h$" EXCLUDE
	REGEX "/client/detail/session_pool[.]h$" EXCLUDE
	REGEX "/transport/tcp_connection[.]h$" EXCLUDE
	REGEX "/transport/tls_connection[.]h$" EXCLUDE
	REGEX "/transport/preface[.]h$" EXCLUDE
)
# These public classes have template definitions in focused detail headers.
# Install only those headers while keeping the remaining protocol details
# private.
install(FILES
	protocol/detail/duplex_channel.h
	protocol/detail/fields.h
	protocol/detail/method.h
	protocol/detail/multipart.h
	protocol/detail/parameters.h
	protocol/detail/request_head.h
	protocol/detail/response_head.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/experimental/http/protocol/detail
)
