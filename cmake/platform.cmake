# SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

option(RIWO_USE_LIBCXX
	"-- ${PRO_NAME}: Use clang libcxx." OFF
)
option(RIWO_USE_LLD
	"-- ${PRO_NAME}: Use clang lld." OFF
)
option(RIWO_ENABLE_LTO
	"-- ${PRO_NAME}: Use gnu-lto." OFF
)
if (RIWO_USE_LIBCXX AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	message(FATAL_ERROR
		"${PRO_NAME}: RIWO_USE_LIBCXX requires the Clang compiler."
	)
endif ()

if (RIWO_USE_LLD AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	message(FATAL_ERROR
		"${PRO_NAME}: RIWO_USE_LLD requires the Clang compiler."
	)
endif ()

if (RIWO_ENABLE_LTO AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	message(FATAL_ERROR
		"${PRO_NAME}: RIWO_ENABLE_LTO requires the GNU compiler."
	)
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	if (CMAKE_CXX_COMPILER_VERSION LESS 17)
		message(FATAL_ERROR "The minimum version of 'Clang' required is 17.")
	endif ()

	if (RIWO_USE_LIBCXX OR RIWO_USE_LLD)
		include(CheckCXXSourceCompiles)

		set(riwo_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
		set(riwo_saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")

		set(riwo_clang_required_flags)
		set(riwo_clang_required_link_options)

		if (RIWO_USE_LIBCXX)
			list(APPEND riwo_clang_required_flags -stdlib=libc++)
			list(APPEND riwo_clang_required_link_options -stdlib=libc++)
		endif ()

		if (RIWO_USE_LLD)
			list(APPEND riwo_clang_required_link_options -fuse-ld=lld)
		endif ()

		string(JOIN " " riwo_clang_required_flags_string
			${riwo_clang_required_flags}
		)
		set(CMAKE_REQUIRED_FLAGS
			"${riwo_saved_required_flags} ${riwo_clang_required_flags_string}"
		)
		set(CMAKE_REQUIRED_LINK_OPTIONS
			${riwo_saved_required_link_options}
			${riwo_clang_required_link_options}
		)
		unset(RIWO_CLANG_TOOLCHAIN_OPTIONS_AVAILABLE CACHE)

		check_cxx_source_compiles (
			"#include <string>\nint main() { std::string value; return value.size(); }"
			RIWO_CLANG_TOOLCHAIN_OPTIONS_AVAILABLE
		)
		set(CMAKE_REQUIRED_FLAGS "${riwo_saved_required_flags}")
		set(CMAKE_REQUIRED_LINK_OPTIONS ${riwo_saved_required_link_options})

		if (NOT RIWO_CLANG_TOOLCHAIN_OPTIONS_AVAILABLE)
			message(FATAL_ERROR
				"${PRO_NAME}: Requested Clang runtime/linker options are unavailable."
			)
		endif ()
	endif ()

	if (RIWO_USE_LIBCXX)
		message(STATUS "${PRO_NAME}: Use clang libcxx.")
		add_compile_options (
			$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>
		)
		add_link_options(-stdlib=libc++)
	endif ()

	if (RIWO_USE_LLD)
		message(STATUS "${PRO_NAME}: Use clang lld.")
		set(CMAKE_EXE_LINKER_FLAGS -fuse-ld=lld)
	endif ()

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	if (CMAKE_CXX_COMPILER_VERSION LESS 13)
		message(FATAL_ERROR "The minimum version of 'GNU' required is 13.")
	endif ()

	if (RIWO_ENABLE_LTO)
		include(CheckIPOSupported)

		check_ipo_supported(RESULT riwo_lto_available
			OUTPUT riwo_lto_error LANGUAGES CXX
		)
		if (NOT riwo_lto_available)
			message(FATAL_ERROR
				"${PRO_NAME}: GNU LTO is unavailable: ${riwo_lto_error}"
			)
		endif ()

		message(STATUS "${PRO_NAME}: Use gnu-lto.")
		add_compile_options(-flto=auto)
	endif ()

elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
	if (MSVC_VERSION LESS 1930)
		message(FATAL_ERROR "The minimum version of 'MSVC' required is 1930 (VS2022).")
	endif ()
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0A00)
	add_compile_options(/Zc:preprocessor /bigobj)

else()
	message(STATUS "Unknow compiler: " ${CMAKE_CXX_COMPILER_ID} " (" ${CMAKE_CXX_COMPILER_VERSION} ").")
endif ()

set(riwo_build_static_default OFF)
set(riwo_gnu_shared_runtime_available TRUE)

if (WIN32)
	set(OS_CPP win)
	set(IS_CPP winnt)
	set(install_dir bin)

	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		execute_process (
			COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++-6.dll
			OUTPUT_VARIABLE riwo_gnu_libstdcxx_dll
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET
		)
		if (NOT IS_ABSOLUTE "${riwo_gnu_libstdcxx_dll}" OR NOT EXISTS "${riwo_gnu_libstdcxx_dll}")
			get_filename_component(riwo_gnu_compiler_dir
				"${CMAKE_CXX_COMPILER}" DIRECTORY
			)
			set(riwo_gnu_libstdcxx_dll
				"${riwo_gnu_compiler_dir}/libstdc++-6.dll"
			)
		endif ()

		if (IS_ABSOLUTE "${riwo_gnu_libstdcxx_dll}" AND EXISTS "${riwo_gnu_libstdcxx_dll}")
			get_filename_component(riwo_gnu_runtime_dir
				"${riwo_gnu_libstdcxx_dll}" DIRECTORY
			)
		endif ()

		if (NOT IS_ABSOLUTE "${riwo_gnu_libstdcxx_dll}" OR NOT EXISTS "${riwo_gnu_libstdcxx_dll}")
			set(riwo_gnu_shared_runtime_available FALSE)
			set(riwo_build_static_default ON)

			message(STATUS
				"${PRO_NAME}: GNU C++ runtime is static-only."
			)
		endif ()
	endif ()

else ()
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		add_compile_options(-rdynamic)
	endif ()

	if (APPLE)
		set(OS_CPP apple)
	elseif (ANDROID)
		set(OS_CPP android)
	else ()
		set(OS_CPP unix)
	endif ()

	set(IS_CPP posix)
	set(install_dir lib)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(RIWO_OUTPUT_DIR ${CMAKE_BINARY_DIR}/output)

message(STATUS "")
message(STATUS "${PRO_NAME}: Using C++: " ${CMAKE_CXX_STANDARD})
message(STATUS "${PRO_NAME}: Build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "${PRO_NAME}: Install prefix: " ${CMAKE_INSTALL_PREFIX})
