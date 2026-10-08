# SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.15)

set(MAJOR_VERSION 1)
set(MINOR_VERSION 0)
set(PATCH_VERSION 1)

set(PRO_VERSION ${MAJOR_VERSION}.${MINOR_VERSION}.${PATCH_VERSION})
set(PRO_NAME Riwo)

project(${PRO_NAME} VERSION ${PRO_VERSION} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_definitions (
	-DRIWO_MAJOR_VERSION=${MAJOR_VERSION}
	-DRIWO_MINOR_VERSION=${MINOR_VERSION}
	-DRIWO_PATCH_VERSION=${PATCH_VERSION}
	-DRIWO_VERSION_STR="${PRO_VERSION}-beta"
)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(platform)
include(heavy_compile)

include(options)
include(dependencies)

add_subdirectory(3rd_party)
include(3rd_party)

include(test)
include(CTest)

if (CMAKE_BUILD_TYPE AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
	add_definitions(-DRIWO_DEBUG)
endif()

include_directories(.)
add_subdirectory(riwo)

# Generate the installed package only after every provider and exported target
# has been resolved.  External providers choose their concrete imported target
# names while configuring 3rd_party, so generating this file earlier leaves
# empty target names in RiwoConfig.cmake.
include(provider)

if (BUILD_TESTING)
	add_subdirectory(test)
endif()

if (RIWO_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES riwo.h)
install(FILES riwo.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
