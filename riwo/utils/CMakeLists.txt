# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(target_name riwo.utils)
add_project(${target_name} riwo.coro)

target_link_libraries(${target_name} PUBLIC riwo.spdlog)
target_compile_definitions(${target_name} PRIVATE RIWO_UTILS_SHARED)

# GCC's LTO optimiser diagnoses spdlog's scoped_padder destructor as a
# possible oversized string resize even though the third-party code guards the
# negative padding path.  Keep that one provider implementation out of LTO so
# Riwo sources retain the full warning set instead of globally disabling
# -Wstringop-overflow.
if (RIWO_ENABLE_LTO AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set_source_files_properties(detail/spdlog.cpp PROPERTIES
		COMPILE_OPTIONS -fno-lto
	)
endif ()

# Keep a single spdlog implementation in riwo.utils instead of instantiating its
# header-only implementation in logger.cpp.  Shared builds export the spdlog
# symbols from the utils library; static builds resolve them from the archive.
if (NOT RIWO_BUILD_STATIC)
	target_compile_definitions(${target_name} PRIVATE spdlog_EXPORTS)
endif ()

option(RIWO_BUILD_UTILITIES_SBUS_UDP
	"-- ${PRO_NAME}: Build Utilities.SoftBus <UDP> interface." ON
)
set(RIWO_UTILS_SBUS_DEFAULT_INTERFACE "local" CACHE STRING
	"Select the Utilities.SoftBus default interface. (Default: local)"
)
set_property(CACHE RIWO_UTILS_SBUS_DEFAULT_INTERFACE
	PROPERTY STRINGS local udp
)
string(TOLOWER "${RIWO_UTILS_SBUS_DEFAULT_INTERFACE}"
	RIWO_UTILS_SBUS_DEFAULT_INTERFACE_NORMALIZED
)
if (RIWO_UTILS_SBUS_DEFAULT_INTERFACE_NORMALIZED STREQUAL "default")
	set(RIWO_UTILS_SBUS_DEFAULT_INTERFACE_NORMALIZED local)
endif ()

if (NOT RIWO_UTILS_SBUS_DEFAULT_INTERFACE_NORMALIZED MATCHES "^(local|udp)$")
	message(FATAL_ERROR
		"${PRO_NAME}: Unknown Utilities.SoftBus interface: "
		"${RIWO_UTILS_SBUS_DEFAULT_INTERFACE}"
	)
endif ()

set(RIWO_UTILS_SBUS_DEFAULT_INTERFACE
	"${RIWO_UTILS_SBUS_DEFAULT_INTERFACE_NORMALIZED}" CACHE STRING
	"Select the Utilities.SoftBus default interface. (Default: local)" FORCE
)
if (RIWO_BUILD_UTILITIES AND
	RIWO_UTILS_SBUS_DEFAULT_INTERFACE STREQUAL "udp" AND
	NOT RIWO_BUILD_UTILITIES_SBUS_UDP)
	message(FATAL_ERROR
		"${PRO_NAME}: Unsupported Utilities.SoftBus interface: "
		"${RIWO_UTILS_SBUS_DEFAULT_INTERFACE}"
	)
endif ()

set(RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
	${RIWO_BUILD_UTILITIES_SBUS_UDP}
)
if (RIWO_BUILD_UTILITIES_SBUS_UDP)
	message(STATUS "${PRO_NAME}: Build Utilities.SoftBus <UDP> interface.")
	target_compile_definitions(${target_name} PUBLIC
		RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT=1
	)
endif ()

set(RIWO_UTILS_SBUS_DEFAULT_INTERFACE_LOCAL 0)
set(RIWO_UTILS_SBUS_DEFAULT_INTERFACE_UDP 0)

if (RIWO_UTILS_SBUS_DEFAULT_INTERFACE STREQUAL "udp")
	set(RIWO_UTILS_SBUS_DEFAULT_INTERFACE_UDP 1)
else ()
	set(RIWO_UTILS_SBUS_DEFAULT_INTERFACE_LOCAL 1)
endif ()

set(RIWO_UTILS_CONFIG_INCLUDE
	${RIWO_OUTPUT_DIR}/config_include
)
set(RIWO_UTILS_CONFIG_INCLUDE
	${RIWO_UTILS_CONFIG_INCLUDE} CACHE PATH
	"Path to riwo config include directory."
)
configure_file (
	${PROJECT_SOURCE_DIR}/riwo/utils/cxx/configs.h.in
	${RIWO_UTILS_CONFIG_INCLUDE}/riwo/utils/cxx/configs.h
	@ONLY
)
include_directories(${RIWO_UTILS_CONFIG_INCLUDE})

install(FILES
	${RIWO_UTILS_CONFIG_INCLUDE}/riwo/utils/cxx/configs.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/utils/cxx
)
