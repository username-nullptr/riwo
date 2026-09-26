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
