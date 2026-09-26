# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

option(RIWO_ADD_LIBRARY_VERSION
	"-- ${PRO_NAME}: Add version information to shared library names." ON
)
set(RIWO_ASIO_PROVIDER "" CACHE STRING
	"-- ${PRO_NAME}: Asio provider (BUNDLED, EXTERNAL, or BOOST); empty uses BUNDLED."
)
set_property(CACHE RIWO_ASIO_PROVIDER PROPERTY STRINGS BUNDLED EXTERNAL BOOST)

string(STRIP "${RIWO_ASIO_PROVIDER}" RIWO_ASIO_PROVIDER)
string(TOUPPER "${RIWO_ASIO_PROVIDER}" RIWO_ASIO_PROVIDER)

if (RIWO_ASIO_PROVIDER STREQUAL "")
	set(RIWO_ASIO_PROVIDER BUNDLED)

elseif (NOT RIWO_ASIO_PROVIDER MATCHES "^(BUNDLED|EXTERNAL|BOOST)$")
	message(FATAL_ERROR
		"${PRO_NAME}: Invalid RIWO_ASIO_PROVIDER='${RIWO_ASIO_PROVIDER}'. "
		"Expected BUNDLED, EXTERNAL, BOOST, or an empty value."
	)
endif ()

set(RIWO_BOOST_INSTALL_PREFIX "" CACHE PATH
	"Install prefix of an external Boost package."
)
set(RIWO_ASIO_INSTALL_PREFIX "" CACHE PATH
	"Install prefix of an external Asio package."
)
set(RIWO_USING_BOOST_ASIO OFF)

if (RIWO_ASIO_PROVIDER STREQUAL "BOOST")
	set(RIWO_USING_BOOST_ASIO ON)
endif ()

option(RIWO_USE_BUNDLED_SPDLOG
	"-- ${PRO_NAME}: Use bundled spdlog." ON
)
set(RIWO_SPDLOG_INSTALL_PREFIX "" CACHE PATH
	"Install prefix of an external spdlog package."
)
add_library(riwo.asio INTERFACE)
add_library(Riwo::asio ALIAS riwo.asio)

set_target_properties(riwo.asio PROPERTIES EXPORT_NAME asio)
set(RIWO_ASIO_PACKAGE_TARGET asio::asio)

# External Asio packages are commonly advertised as header-only.  Riwo keeps
# exactly one implementation in riwo.core, so explicitly cancel any provider
# definition that would make headers include their .ipp implementation files.
if (MSVC)
	set(riwo_undefine_asio_header_only
		/UASIO_HEADER_ONLY /UBOOST_ASIO_HEADER_ONLY
	)
else ()
	set(riwo_undefine_asio_header_only
		-UASIO_HEADER_ONLY -UBOOST_ASIO_HEADER_ONLY
	)
endif ()

target_compile_options(riwo.asio INTERFACE
	"$<$<COMPILE_LANGUAGE:CXX>:${riwo_undefine_asio_header_only}>"
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(riwo.asio INTERFACE
		-Wno-mismatched-new-delete
	)
endif ()

if (RIWO_ASIO_PROVIDER STREQUAL "BOOST")
	message(STATUS "${PRO_NAME}: Use external Boost.Asio.")

	if (RIWO_BOOST_INSTALL_PREFIX)
		list(PREPEND CMAKE_PREFIX_PATH "${RIWO_BOOST_INSTALL_PREFIX}")
		set(Boost_ROOT "${RIWO_BOOST_INSTALL_PREFIX}")
	endif ()

	find_package(Boost CONFIG REQUIRED)
	if (TARGET Boost::headers)
		set(RIWO_ASIO_PACKAGE_TARGET Boost::headers)
	elseif (TARGET Boost::boost)
		set(RIWO_ASIO_PACKAGE_TARGET Boost::boost)
	else ()
		message(FATAL_ERROR
			"${PRO_NAME}: The external Boost package does not provide a headers target."
		)
	endif ()

	target_link_libraries(riwo.asio INTERFACE ${RIWO_ASIO_PACKAGE_TARGET})
	target_compile_definitions(riwo.asio INTERFACE RIWO_USING_BOOST_ASIO=1)

elseif (RIWO_ASIO_PROVIDER STREQUAL "BUNDLED")
	message(STATUS "${PRO_NAME}: Use bundled standalone Asio.")

	target_include_directories(riwo.asio SYSTEM INTERFACE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/asio>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	)
	target_compile_definitions(riwo.asio INTERFACE ASIO_STANDALONE=1)

	add_subdirectory(asio)

elseif (RIWO_ASIO_PROVIDER STREQUAL "EXTERNAL")
	message(STATUS "${PRO_NAME}: Use external standalone Asio.")
	find_package(asio CONFIG QUIET HINTS "${RIWO_ASIO_INSTALL_PREFIX}")

	if (NOT TARGET asio::asio AND NOT TARGET Asio::asio)
		find_package(Asio CONFIG QUIET HINTS "${RIWO_ASIO_INSTALL_PREFIX}")
	endif ()

	if (TARGET asio::asio)
		set(RIWO_ASIO_PACKAGE_TARGET asio::asio)
	elseif (TARGET Asio::asio)
		set(RIWO_ASIO_PACKAGE_TARGET Asio::asio)
	else ()
		find_path(RIWO_ASIO_INCLUDE_DIR
			NAMES asio.hpp
			HINTS "${RIWO_ASIO_INSTALL_PREFIX}"
			PATH_SUFFIXES include
		)
		if (NOT RIWO_ASIO_INCLUDE_DIR)
			message(FATAL_ERROR
				"${PRO_NAME}: External standalone Asio was requested, but asio.hpp was not found. "
				"Set RIWO_ASIO_INSTALL_PREFIX to its install prefix."
			)
		endif ()

		add_library(asio::asio INTERFACE IMPORTED GLOBAL)
		set_target_properties(asio::asio PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${RIWO_ASIO_INCLUDE_DIR}"
		)
		set(RIWO_ASIO_PACKAGE_TARGET asio::asio)
		mark_as_advanced(RIWO_ASIO_INCLUDE_DIR)
	endif ()

	target_link_libraries(riwo.asio INTERFACE ${RIWO_ASIO_PACKAGE_TARGET})
	target_compile_definitions(riwo.asio INTERFACE ASIO_STANDALONE=1)
endif ()

add_library(riwo.spdlog INTERFACE)
add_library(Riwo::spdlog ALIAS riwo.spdlog)

set_target_properties(riwo.spdlog PROPERTIES EXPORT_NAME spdlog)
set(RIWO_SPDLOG_PACKAGE_TARGET spdlog::spdlog_header_only)

# A provider may itself be header-only, but riwo.utils owns the one compiled
# spdlog implementation.  Consumers must see declarations/imports only.
target_compile_definitions(riwo.spdlog INTERFACE
	SPDLOG_COMPILED_LIB
	SPDLOG_USE_STD_FORMAT
)
if (NOT RIWO_BUILD_STATIC)
	target_compile_definitions(riwo.spdlog INTERFACE SPDLOG_SHARED_LIB)
endif ()

if (WIN32)
	target_compile_definitions(riwo.spdlog INTERFACE SPDLOG_WCHAR_FILENAMES)
endif ()

if (MSVC)
	set(riwo_undefine_spdlog_header_only /USPDLOG_HEADER_ONLY)
else ()
	set(riwo_undefine_spdlog_header_only -USPDLOG_HEADER_ONLY)
endif ()

target_compile_options(riwo.spdlog INTERFACE
	"$<$<COMPILE_LANGUAGE:CXX>:${riwo_undefine_spdlog_header_only}>"
)
if (RIWO_USE_BUNDLED_SPDLOG)
	message(STATUS "${PRO_NAME}: Use bundled spdlog.")

	target_include_directories(riwo.spdlog SYSTEM INTERFACE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/spdlog>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	)
	add_subdirectory(spdlog)
	set(RIWO_SPDLOG_PROVIDER bundled)

else ()
	message(STATUS "${PRO_NAME}: Use external spdlog.")
	find_package(spdlog CONFIG QUIET HINTS "${RIWO_SPDLOG_INSTALL_PREFIX}")

	if (TARGET spdlog::spdlog_header_only)
		set(RIWO_SPDLOG_PACKAGE_TARGET spdlog::spdlog_header_only)
	elseif (TARGET spdlog::spdlog)
		set(RIWO_SPDLOG_PACKAGE_TARGET spdlog::spdlog)
	else ()
		find_path(RIWO_SPDLOG_INCLUDE_DIR
			NAMES spdlog/spdlog.h
			HINTS "${RIWO_SPDLOG_INSTALL_PREFIX}"
			PATH_SUFFIXES include
		)
		if (NOT RIWO_SPDLOG_INCLUDE_DIR)
			message(FATAL_ERROR
				"${PRO_NAME}: External spdlog was requested, but spdlog/spdlog.h was not found. "
				"Set RIWO_SPDLOG_INSTALL_PREFIX to its install prefix."
			)
		endif ()

		add_library(spdlog::spdlog_header_only INTERFACE IMPORTED GLOBAL)
		set_target_properties(spdlog::spdlog_header_only PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${RIWO_SPDLOG_INCLUDE_DIR}"
		)
		set(RIWO_SPDLOG_PACKAGE_TARGET spdlog::spdlog_header_only)
		mark_as_advanced(RIWO_SPDLOG_INCLUDE_DIR)
	endif ()

	target_link_libraries(riwo.spdlog INTERFACE ${RIWO_SPDLOG_PACKAGE_TARGET})
	set(RIWO_SPDLOG_PROVIDER external)
endif ()

install(TARGETS riwo.asio EXPORT RiwoTargets)
if (RIWO_BUILD_UTILITIES)
	install(TARGETS riwo.spdlog EXPORT RiwoTargets)
endif ()

# These values are needed while generating the installed package config in the
# parent directory.
set(RIWO_ASIO_PROVIDER "${RIWO_ASIO_PROVIDER}" PARENT_SCOPE)
set(RIWO_ASIO_PACKAGE_TARGET "${RIWO_ASIO_PACKAGE_TARGET}" PARENT_SCOPE)

set(RIWO_SPDLOG_PROVIDER "${RIWO_SPDLOG_PROVIDER}" PARENT_SCOPE)
set(RIWO_SPDLOG_PACKAGE_TARGET "${RIWO_SPDLOG_PACKAGE_TARGET}" PARENT_SCOPE)
