# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(RIWO_HTTP_EXP_ZLIB_SUPPORT 0)
find_package(ZLIB QUIET)

if (ZLIB_FOUND OR TARGET ZLIB::ZLIB)
	set(RIWO_HTTP_EXP_ZLIB_SUPPORT 1)
endif ()

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

if (RIWO_HTTP_EXP_HTTP2_SUPPORT)
	target_link_libraries(${target_name} PRIVATE Riwo::http_exp_nghttp2)
endif ()

if (RIWO_HTTP_EXP_ZLIB_SUPPORT)
	target_link_libraries(${target_name} PRIVATE ZLIB::ZLIB)
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
