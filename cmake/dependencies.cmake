# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

function(riwo_validate_msvc_imported_targets package root_option)
	if (NOT MSVC)
		return()
	endif ()

	foreach(target IN LISTS ARGN)
		if (NOT TARGET ${target})
			continue()
		endif ()

		set(properties IMPORTED_LOCATION IMPORTED_IMPLIB)
		get_target_property(configurations ${target} IMPORTED_CONFIGURATIONS)

		foreach(configuration IN LISTS configurations)
			string(TOUPPER "${configuration}" configuration_upper)
			list(APPEND properties
				IMPORTED_LOCATION_${configuration_upper}
				IMPORTED_IMPLIB_${configuration_upper}
			)
		endforeach()

		foreach(property IN LISTS properties)
			get_target_property(paths ${target} ${property})
			if (NOT paths OR paths MATCHES "-NOTFOUND$")
				continue()
			endif ()

			foreach(path IN LISTS paths)
				string(TOLOWER "${path}" path_lower)
				if (path_lower MATCHES "mingw" OR path_lower MATCHES "\\.a$")
					message(FATAL_ERROR
						"${PRO_NAME}: ${package} target '${target}' resolves to "
						"'${path}', which is incompatible with MSVC. Install an "
						"MSVC-native ${package} package and set -D${root_option}=<prefix>."
					)
				endif ()
			endforeach()
		endforeach()
	endforeach()
endfunction()


# A few static OpenSSL distributions are built with zlib enabled but omit
# zlib from OpenSSL::Crypto's imported link interface.  Core's logical
# dependency remains OpenSSL; this function repairs only that imported
# target's incomplete physical link closure when a link probe proves the
# extra edge is necessary.
function(riwo_complete_openssl_link_closure)
	set(RIWO_PACKAGE_OPENSSL_NEEDS_ZLIB OFF CACHE INTERNAL
		"Whether the selected OpenSSL package needs an implicit zlib link edge."
		FORCE
	)
	if (NOT TARGET OpenSSL::SSL OR NOT TARGET OpenSSL::Crypto)
		return()
	endif ()

	include(CMakePushCheckState)
	include(CheckCSourceCompiles)

	# Match the physical platform closure used by riwo.core. MinGW's static
	# OpenSSL package does not advertise these Windows libraries either.
	set(openssl_probe_system_libraries)
	if (MINGW)
		set(openssl_probe_system_libraries
			ws2_32 wsock32 crypt32 bcrypt ole32 shell32 uuid
		)
	endif ()

	set(openssl_probe [=[
#include <openssl/comp.h>
#include <openssl/ssl.h>
int main(void)
{
	SSL_CTX *context = SSL_CTX_new(TLS_method());
	COMP_METHOD *compression = COMP_zlib();
	SSL_CTX_free(context);
	return context == 0 || compression == 0;
}
]=])
	cmake_push_check_state(RESET)

	set(CMAKE_REQUIRED_QUIET TRUE)
	set(CMAKE_REQUIRED_LIBRARIES
		OpenSSL::SSL OpenSSL::Crypto
		${openssl_probe_system_libraries}
	)
	unset(RIWO_OPENSSL_LINKS_WITHOUT_ZLIB CACHE)

	check_c_source_compiles("${openssl_probe}"
		RIWO_OPENSSL_LINKS_WITHOUT_ZLIB
	)
	cmake_pop_check_state()

	if (RIWO_OPENSSL_LINKS_WITHOUT_ZLIB)
		return()
	endif ()

	if (RIWO_ZLIB_INSTALL_PREFIX)
		set(ZLIB_ROOT "${RIWO_ZLIB_INSTALL_PREFIX}")
	endif ()

	find_package(ZLIB QUIET)
	if (NOT TARGET ZLIB::ZLIB)
		return()
	endif ()

	cmake_push_check_state(RESET)

	set(CMAKE_REQUIRED_QUIET TRUE)
	set(CMAKE_REQUIRED_LIBRARIES
		OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB
		${openssl_probe_system_libraries}
	)
	unset(RIWO_OPENSSL_LINKS_WITH_ZLIB CACHE)

	check_c_source_compiles("${openssl_probe}"
		RIWO_OPENSSL_LINKS_WITH_ZLIB
	)
	cmake_pop_check_state()

	if (NOT RIWO_OPENSSL_LINKS_WITH_ZLIB)
		return()
	endif ()

	get_target_property(openssl_crypto_links
		OpenSSL::Crypto INTERFACE_LINK_LIBRARIES
	)
	if (NOT ZLIB::ZLIB IN_LIST openssl_crypto_links)
		set_property(TARGET OpenSSL::Crypto APPEND PROPERTY
			INTERFACE_LINK_LIBRARIES ZLIB::ZLIB
		)
	endif ()

	set(RIWO_PACKAGE_OPENSSL_NEEDS_ZLIB ON CACHE INTERNAL
		"Whether the selected OpenSSL package needs an implicit zlib link edge."
		FORCE
	)
	message(STATUS
		"${PRO_NAME}: Complete the selected OpenSSL package's implicit zlib link closure."
	)
endfunction()


# Resolve nghttp2 from the supported providers, verify the exact HTTP/2 API
# surface used by EXP_HTTP, and publish the normalized result to the caller.
function(riwo_configure_exp_http_nghttp2)
	set(http2_support 0)

	set(nghttp2_provider "none")
	set(nghttp2_version "unavailable")
	set(nghttp2_linkage "unavailable")

	set(nghttp2_api "unavailable")
	set(nghttp2_use_modern_api 0)
	set(nghttp2_target "")

	if (NOT RIWO_EXP_HTTP2_SUPPORT STREQUAL "OFF")
		find_package(nghttp2 CONFIG QUIET)

		foreach(candidate
			nghttp2::nghttp2
			nghttp2::nghttp2_static
			nghttp2::nghttp2_shared
		)
			if (TARGET ${candidate})
				set(nghttp2_target ${candidate})
				set(nghttp2_provider "config")
				break()
			endif ()
		endforeach()

		if (NOT nghttp2_target)
			find_package(PkgConfig QUIET)
			if (PkgConfig_FOUND)
				pkg_check_modules(RIWO_NGHTTP2 QUIET IMPORTED_TARGET libnghttp2)
				if (TARGET PkgConfig::RIWO_NGHTTP2)
					set(nghttp2_target PkgConfig::RIWO_NGHTTP2)
					set(nghttp2_provider "pkg-config")
					set(nghttp2_version "${RIWO_NGHTTP2_VERSION}")
				endif ()
			endif ()
		endif ()

		if (NOT nghttp2_target)
			find_path(RIWO_NGHTTP2_INCLUDE_DIR NAMES nghttp2/nghttp2.h)
			find_library(RIWO_NGHTTP2_LIBRARY NAMES nghttp2 libnghttp2)

			if (RIWO_NGHTTP2_INCLUDE_DIR AND RIWO_NGHTTP2_LIBRARY)
				add_library(riwo_exp_http_nghttp2_library UNKNOWN IMPORTED)
				set_target_properties(riwo_exp_http_nghttp2_library PROPERTIES
					IMPORTED_LOCATION "${RIWO_NGHTTP2_LIBRARY}"
					INTERFACE_INCLUDE_DIRECTORIES "${RIWO_NGHTTP2_INCLUDE_DIR}"
				)
				set(nghttp2_target riwo_exp_http_nghttp2_library)
				set(nghttp2_provider "library")
			endif ()
		endif ()

		if (nghttp2_target)
			if (nghttp2_version STREQUAL "unavailable")
				if (DEFINED nghttp2_VERSION)
					set(nghttp2_version "${nghttp2_VERSION}")

				elseif (RIWO_NGHTTP2_INCLUDE_DIR AND
					EXISTS "${RIWO_NGHTTP2_INCLUDE_DIR}/nghttp2/nghttp2ver.h")
					file(STRINGS
						"${RIWO_NGHTTP2_INCLUDE_DIR}/nghttp2/nghttp2ver.h"
						nghttp2_version_line
						REGEX "^#define NGHTTP2_VERSION \"[0-9.]+\""
					)
					string(REGEX REPLACE
						"^#define NGHTTP2_VERSION \"([0-9.]+)\".*" "\\1"
						nghttp2_version
						"${nghttp2_version_line}"
					)
				endif ()
			endif ()

			include(CMakePushCheckState)
			include(CheckCSourceCompiles)

			cmake_push_check_state(RESET)

			set(CMAKE_REQUIRED_QUIET TRUE)
			set(CMAKE_REQUIRED_LIBRARIES ${nghttp2_target})

			unset(RIWO_EXP_HTTP_NGHTTP2_MODERN_CAPABILITIES CACHE)

			check_c_source_compiles([=[
#define NGHTTP2_NO_SSIZE_T
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
    nghttp2_data_provider2 provider = {0};
    nghttp2_settings_entry setting = {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100};

    if (nghttp2_session_callbacks_new(&callbacks) != 0) return 1;
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
    if (nghttp2_option_new(&option) != 0) return 2;
    nghttp2_option_set_no_auto_window_update(option, 1);
    if (nghttp2_session_client_new2(&client, callbacks, 0, option) != 0) return 3;
    if (nghttp2_session_server_new2(&server, callbacks, 0, option) != 0) return 4;
    (void)nghttp2_session_mem_recv2(client, 0, 0);
    (void)nghttp2_session_mem_send2(client, &output);
    (void)nghttp2_submit_request2(client, 0, 0, 0, &provider, 0);
    (void)nghttp2_submit_response2(server, 1, 0, 0, &provider);
    (void)nghttp2_session_resume_data(client, 1);
    (void)nghttp2_session_consume(client, 1, 1);
    (void)nghttp2_session_get_remote_settings(client,
        NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    (void)nghttp2_submit_rst_stream(client, NGHTTP2_FLAG_NONE, 1,
        NGHTTP2_CANCEL);
    (void)nghttp2_submit_trailer(client, 1, 0, 0);
    (void)nghttp2_submit_settings(client, NGHTTP2_FLAG_NONE, &setting, 1);
    (void)nghttp2_submit_goaway(client, NGHTTP2_FLAG_NONE, 0,
        NGHTTP2_NO_ERROR, 0, 0);
    nghttp2_session_del(client);
    nghttp2_session_del(server);
    nghttp2_option_del(option);
    nghttp2_session_callbacks_del(callbacks);
    return 0;
}
]=] RIWO_EXP_HTTP_NGHTTP2_MODERN_CAPABILITIES)

			unset(RIWO_EXP_HTTP_NGHTTP2_LEGACY_CAPABILITIES CACHE)

			check_c_source_compiles([=[
#if defined(_MSC_VER) && !defined(_SSIZE_T_DEFINED)
# include <BaseTsd.h>
typedef SSIZE_T ssize_t;
# define _SSIZE_T_DEFINED
#endif
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
    nghttp2_data_provider provider = {0};
    nghttp2_settings_entry setting = {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100};

    if (nghttp2_session_callbacks_new(&callbacks) != 0) return 1;
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
    if (nghttp2_option_new(&option) != 0) return 2;
    nghttp2_option_set_no_auto_window_update(option, 1);
    if (nghttp2_session_client_new2(&client, callbacks, 0, option) != 0) return 3;
    if (nghttp2_session_server_new2(&server, callbacks, 0, option) != 0) return 4;
    (void)nghttp2_session_mem_recv(client, 0, 0);
    (void)nghttp2_session_mem_send(client, &output);
    (void)nghttp2_submit_request(client, 0, 0, 0, &provider, 0);
    (void)nghttp2_submit_response(server, 1, 0, 0, &provider);
    (void)nghttp2_session_resume_data(client, 1);
    (void)nghttp2_session_consume(client, 1, 1);
    (void)nghttp2_session_get_remote_settings(client,
        NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    (void)nghttp2_submit_rst_stream(client, NGHTTP2_FLAG_NONE, 1,
        NGHTTP2_CANCEL);
    (void)nghttp2_submit_trailer(client, 1, 0, 0);
    (void)nghttp2_submit_settings(client, NGHTTP2_FLAG_NONE, &setting, 1);
    (void)nghttp2_submit_goaway(client, NGHTTP2_FLAG_NONE, 0,
        NGHTTP2_NO_ERROR, 0, 0);
    nghttp2_session_del(client);
    nghttp2_session_del(server);
    nghttp2_option_del(option);
    nghttp2_session_callbacks_del(callbacks);
    return 0;
}
]=] RIWO_EXP_HTTP_NGHTTP2_LEGACY_CAPABILITIES)
			cmake_pop_check_state()

			if (RIWO_EXP_HTTP_NGHTTP2_MODERN_CAPABILITIES OR
				RIWO_EXP_HTTP_NGHTTP2_LEGACY_CAPABILITIES)
				set(http2_support 1)

				if (RIWO_EXP_HTTP_NGHTTP2_MODERN_CAPABILITIES)
					set(nghttp2_api "modern")
					set(nghttp2_use_modern_api 1)
				else ()
					set(nghttp2_api "legacy")
				endif ()

				add_library(Riwo::exp_http_nghttp2 INTERFACE IMPORTED GLOBAL)
				set_property(TARGET Riwo::exp_http_nghttp2 PROPERTY
					INTERFACE_LINK_LIBRARIES ${nghttp2_target}
				)
				get_target_property(nghttp2_type ${nghttp2_target} TYPE)
				if (nghttp2_type)
					string(TOLOWER "${nghttp2_type}" nghttp2_linkage)
				endif ()
			endif ()
		endif ()

		if (RIWO_EXP_HTTP2_SUPPORT STREQUAL "ON" AND NOT http2_support)
			message(FATAL_ERROR
				"${PRO_NAME}: RIWO_EXP_HTTP2_SUPPORT=ON requires an nghttp2 library "
				"with the client/server, callback, manual flow-control, memory I/O, "
				"resume DATA, SETTINGS and GOAWAY APIs."
			)
		endif ()
	endif ()

	set(RIWO_EXP_HTTP_HTTP2_SUPPORT ${http2_support} PARENT_SCOPE)

	set(RIWO_EXP_HTTP_NGHTTP2_PROVIDER "${nghttp2_provider}" PARENT_SCOPE)
	set(RIWO_EXP_HTTP_NGHTTP2_VERSION "${nghttp2_version}" PARENT_SCOPE)
	set(RIWO_EXP_HTTP_NGHTTP2_LINKAGE "${nghttp2_linkage}" PARENT_SCOPE)
	set(RIWO_EXP_HTTP_NGHTTP2_API "${nghttp2_api}" PARENT_SCOPE)

	set(RIWO_EXP_HTTP_NGHTTP2_USE_MODERN_API
		${nghttp2_use_modern_api} PARENT_SCOPE
	)
endfunction()
