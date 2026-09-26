# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(target_name riwo.core)
add_project(${target_name} riwo.asio)

# Riwo exposes Asio error codes across module and application boundaries.  A
# header-only Asio build creates a separate error_category instance in every
# Windows DLL, so equal error values compare unequal after crossing a DLL
# boundary.  Keep one Asio implementation in riwo.core and make every dependent
# target use it.
if (RIWO_USING_BOOST_ASIO)
	set(asio_separate_compilation BOOST_ASIO_SEPARATE_COMPILATION)
	set(asio_dynamic_link BOOST_ASIO_DYN_LINK)
	set(asio_declaration BOOST_ASIO_DECL)
else ()
	set(asio_separate_compilation ASIO_SEPARATE_COMPILATION)
	set(asio_dynamic_link ASIO_DYN_LINK)
	set(asio_declaration ASIO_DECL)
endif ()

if (RIWO_BUILD_STATIC)
	target_compile_definitions(${target_name} PUBLIC ${asio_separate_compilation})
else ()
	# Sources inside riwo.core resolve the implementation directly. Dependents
	# import the same symbols from the DLL.
	target_compile_definitions(${target_name}
		PRIVATE ${asio_separate_compilation}
		INTERFACE ${asio_dynamic_link}
	)
	# Asio's own visibility header applies dllimport/dllexport for MSVC but not
	# for MinGW.  Without an explicit declaration the implementation is present
	# in riwo.core.dll yet absent from its import library, so dependent Riwo DLLs
	# cannot link.  Publish the import decoration to dependents and consumers.
	if (MINGW)
		target_compile_definitions(${target_name}
			PRIVATE "${asio_declaration}=__declspec(dllexport)"
			INTERFACE "${asio_declaration}=__declspec(dllimport)"
		)
	endif ()
endif ()

if (RIWO_OPENSSL_SUPPORT)
	if (RIWO_OPENSSL_INSTALL_PREFIX)
		set(OPENSSL_ROOT_DIR "${RIWO_OPENSSL_INSTALL_PREFIX}")
	endif ()
	find_package(OpenSSL REQUIRED)

	riwo_validate_msvc_imported_targets(OpenSSL
		RIWO_OPENSSL_INSTALL_PREFIX OpenSSL::SSL OpenSSL::Crypto
	)
	riwo_complete_openssl_link_closure()
	target_link_libraries(${target_name} PUBLIC
		OpenSSL::SSL OpenSSL::Crypto
	)
endif ()

if (UNIX)
	target_link_libraries(${target_name} PUBLIC pthread dl)
elseif (WIN32 AND (
	CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
	CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
	# OpenSSL is an Asio dependency and is linked above. Keep its Windows API
	# closure after the static archive; compression remains in HTTP/WebSocket.
	target_link_libraries(${target_name} PUBLIC
		ws2_32 wsock32 crypt32 bcrypt ole32 shell32 uuid
	)
endif()

if (RIWO_IO_URING_SUPPORT)
	find_path(LIBURING_INCLUDE_DIR NAMES liburing.h)
	find_library(LIBURING_LIBRARY NAMES uring)

	if (NOT LIBURING_INCLUDE_DIR OR NOT LIBURING_LIBRARY)
		message(FATAL_ERROR
			"${PRO_NAME}: Linux I/O uring support requires liburing."
		)
	endif ()

	if (RIWO_USING_BOOST_ASIO)
		target_compile_definitions(${target_name} PUBLIC
			RIWO_IO_URING_SUPPORT=1
			BOOST_ASIO_HAS_IO_URING=1
			BOOST_ASIO_DISABLE_EPOLL=1
		)
	else ()
		target_compile_definitions(${target_name} PUBLIC
			RIWO_IO_URING_SUPPORT=1
			ASIO_HAS_IO_URING=1
			ASIO_DISABLE_EPOLL=1
		)
	endif ()
	add_library(Riwo::uring UNKNOWN IMPORTED)

	set_target_properties(Riwo::uring PROPERTIES
		IMPORTED_LOCATION "${LIBURING_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${LIBURING_INCLUDE_DIR}"
	)
	target_link_libraries(${target_name} PUBLIC Riwo::uring)
	mark_as_advanced(LIBURING_INCLUDE_DIR LIBURING_LIBRARY)
endif ()
