# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(RIWO_HTTP_EXP_HTTP2_SUPPORT 0)
set(RIWO_HTTP_EXP_NGHTTP2_PROVIDER "none")
set(RIWO_HTTP_EXP_NGHTTP2_VERSION "unavailable")
set(RIWO_HTTP_EXP_NGHTTP2_LINKAGE "unavailable")
set(riwo_http_exp_nghttp2_target "")

if (NOT RIWO_EXP_HTTP2_SUPPORT STREQUAL "OFF")
	find_package(nghttp2 CONFIG QUIET)
	foreach(candidate
		nghttp2::nghttp2
		nghttp2::nghttp2_static
		nghttp2::nghttp2_shared
	)
		if (TARGET ${candidate})
			set(riwo_http_exp_nghttp2_target ${candidate})
			set(RIWO_HTTP_EXP_NGHTTP2_PROVIDER "config")
			break()
		endif ()
	endforeach()

	if (NOT riwo_http_exp_nghttp2_target)
		find_package(PkgConfig QUIET)
		if (PkgConfig_FOUND)
			pkg_check_modules(RIWO_NGHTTP2 QUIET IMPORTED_TARGET libnghttp2)
			if (TARGET PkgConfig::RIWO_NGHTTP2)
				set(riwo_http_exp_nghttp2_target PkgConfig::RIWO_NGHTTP2)
				set(RIWO_HTTP_EXP_NGHTTP2_PROVIDER "pkg-config")
				set(RIWO_HTTP_EXP_NGHTTP2_VERSION "${RIWO_NGHTTP2_VERSION}")
			endif ()
		endif ()
	endif ()

	if (NOT riwo_http_exp_nghttp2_target)
		find_path(RIWO_NGHTTP2_INCLUDE_DIR NAMES nghttp2/nghttp2.h)
		find_library(RIWO_NGHTTP2_LIBRARY NAMES nghttp2 libnghttp2)
		if (RIWO_NGHTTP2_INCLUDE_DIR AND RIWO_NGHTTP2_LIBRARY)
			add_library(riwo_http_exp_nghttp2_library UNKNOWN IMPORTED)
			set_target_properties(riwo_http_exp_nghttp2_library PROPERTIES
				IMPORTED_LOCATION "${RIWO_NGHTTP2_LIBRARY}"
				INTERFACE_INCLUDE_DIRECTORIES "${RIWO_NGHTTP2_INCLUDE_DIR}"
			)
			set(riwo_http_exp_nghttp2_target riwo_http_exp_nghttp2_library)
			set(RIWO_HTTP_EXP_NGHTTP2_PROVIDER "library")
		endif ()
	endif ()

	if (riwo_http_exp_nghttp2_target)
		include(CMakePushCheckState)
		include(CheckCSourceCompiles)
		cmake_push_check_state(RESET)
		set(CMAKE_REQUIRED_QUIET TRUE)
		set(CMAKE_REQUIRED_LIBRARIES ${riwo_http_exp_nghttp2_target})
		unset(RIWO_HTTP_EXP_NGHTTP2_CAPABILITIES CACHE)
		check_c_source_compiles([=[
#include <nghttp2/nghttp2.h>

static int on_frame_recv(nghttp2_session *session,
                         const nghttp2_frame *frame, void *user_data)
{
    (void)session; (void)frame; (void)user_data;
    return 0;
}

int main(void)
{
    nghttp2_session *client = 0;
    nghttp2_session *server = 0;
    nghttp2_session_callbacks *callbacks = 0;
    nghttp2_option *option = 0;
    const uint8_t *output = 0;
    nghttp2_settings_entry setting = {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100};

    if (nghttp2_session_callbacks_new(&callbacks) != 0) return 1;
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
    if (nghttp2_option_new(&option) != 0) return 2;
    nghttp2_option_set_no_auto_window_update(option, 1);
    if (nghttp2_session_client_new2(&client, callbacks, 0, option) != 0) return 3;
    if (nghttp2_session_server_new2(&server, callbacks, 0, option) != 0) return 4;
    (void)nghttp2_session_mem_recv(client, 0, 0);
    (void)nghttp2_session_mem_send(client, &output);
    (void)nghttp2_session_resume_data(client, 1);
    (void)nghttp2_submit_settings(client, NGHTTP2_FLAG_NONE, &setting, 1);
    (void)nghttp2_submit_goaway(client, NGHTTP2_FLAG_NONE, 0,
        NGHTTP2_NO_ERROR, 0, 0);
    nghttp2_session_del(client);
    nghttp2_session_del(server);
    nghttp2_option_del(option);
    nghttp2_session_callbacks_del(callbacks);
    return 0;
}
]=] RIWO_HTTP_EXP_NGHTTP2_CAPABILITIES)
		cmake_pop_check_state()

		if (RIWO_HTTP_EXP_NGHTTP2_CAPABILITIES)
			set(RIWO_HTTP_EXP_HTTP2_SUPPORT 1)
			add_library(Riwo::http_exp_nghttp2 INTERFACE IMPORTED GLOBAL)
			set_property(TARGET Riwo::http_exp_nghttp2 PROPERTY
				INTERFACE_LINK_LIBRARIES ${riwo_http_exp_nghttp2_target}
			)

			get_target_property(riwo_nghttp2_type
				${riwo_http_exp_nghttp2_target} TYPE
			)
			if (riwo_nghttp2_type)
				string(TOLOWER "${riwo_nghttp2_type}" RIWO_HTTP_EXP_NGHTTP2_LINKAGE)
			endif ()
		endif ()
	endif ()

	if (RIWO_EXP_HTTP2_SUPPORT STREQUAL "ON" AND
		NOT RIWO_HTTP_EXP_HTTP2_SUPPORT)
		message(FATAL_ERROR
			"${PRO_NAME}: RIWO_EXP_HTTP2_SUPPORT=ON requires an nghttp2 library "
			"with the client/server, callback, manual flow-control, memory I/O, "
			"resume DATA, SETTINGS and GOAWAY APIs."
		)
	endif ()
endif ()

message(STATUS
	"${PRO_NAME}: HTTP_EXP HTTP/2 support: ${RIWO_HTTP_EXP_HTTP2_SUPPORT}; "
	"nghttp2 provider=${RIWO_HTTP_EXP_NGHTTP2_PROVIDER}, "
	"version=${RIWO_HTTP_EXP_NGHTTP2_VERSION}, "
	"linkage=${RIWO_HTTP_EXP_NGHTTP2_LINKAGE}."
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

set(RIWO_HTTP_EXP_HTTP2_SUPPORT ${RIWO_HTTP_EXP_HTTP2_SUPPORT} PARENT_SCOPE)
set(RIWO_HTTP_EXP_NGHTTP2_PROVIDER "${RIWO_HTTP_EXP_NGHTTP2_PROVIDER}" PARENT_SCOPE)
set(RIWO_HTTP_EXP_NGHTTP2_VERSION "${RIWO_HTTP_EXP_NGHTTP2_VERSION}" PARENT_SCOPE)
set(RIWO_HTTP_EXP_NGHTTP2_LINKAGE "${RIWO_HTTP_EXP_NGHTTP2_LINKAGE}" PARENT_SCOPE)
