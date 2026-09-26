# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.15)
project(riwo_install_consumer LANGUAGES CXX)

if (RIWO_REQUEST_MISSING_COMPONENT)
	find_package(Riwo CONFIG QUIET COMPONENTS
		${RIWO_REQUEST_MISSING_COMPONENT}
	)
	if (Riwo_FOUND)
		message(FATAL_ERROR
			"Riwo unexpectedly provides component "
			"'${RIWO_REQUEST_MISSING_COMPONENT}'."
		)
	endif ()

	message(STATUS
		"Riwo correctly rejected component '${RIWO_REQUEST_MISSING_COMPONENT}'."
	)
	return()
endif ()

if (NOT RIWO_EXPECTED_COMPONENTS)
	message(FATAL_ERROR "RIWO_EXPECTED_COMPONENTS is required.")
endif ()

string(REPLACE "," ";" riwo_components "${RIWO_EXPECTED_COMPONENTS}")

find_package(Riwo ${RIWO_EXPECTED_VERSION} EXACT CONFIG REQUIRED
	COMPONENTS ${riwo_components}
)
enable_testing()

set(riwo_source_targets)
set(riwo_expected_definitions)

if (WIN32 AND RIWO_RUNTIME_DIR)
	file(GLOB riwo_runtime_dlls "${RIWO_RUNTIME_DIR}/*.dll")

	# The installed package test must be independent of the caller's PATH.  A
	# machine can have several MinGW distributions whose runtime DLLs have the
	# same names but incompatible symbol sets, so copy the DLLs reported by the
	# compiler that built this consumer beside each test executable.
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		get_filename_component(compiler_runtime_dir
			"${CMAKE_CXX_COMPILER}" DIRECTORY
		)
		foreach(runtime_name
			libstdc++-6.dll
			libgcc_s_seh-1.dll
			libgcc_s_dw2-1.dll
			libgcc_s_sjlj-1.dll
			libwinpthread-1.dll
		)
			execute_process(
				COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${runtime_name}
				OUTPUT_VARIABLE runtime_path
				OUTPUT_STRIP_TRAILING_WHITESPACE
				ERROR_QUIET
			)
			if (NOT IS_ABSOLUTE "${runtime_path}" OR NOT EXISTS "${runtime_path}")
				set(runtime_path "${compiler_runtime_dir}/${runtime_name}")
			endif ()
			if (IS_ABSOLUTE "${runtime_path}" AND EXISTS "${runtime_path}")
				list(APPEND riwo_runtime_dlls "${runtime_path}")
			endif ()
		endforeach()
		list(REMOVE_DUPLICATES riwo_runtime_dlls)
	endif ()
endif ()

function(add_riwo_consumer_runtime_test target)
	# Keep compatibility with CMake 3.15, which predates TARGET_RUNTIME_DLLS and
	# CTest's ENVIRONMENT_MODIFICATION property.
	if (riwo_runtime_dlls)
		add_custom_command(TARGET ${target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				${riwo_runtime_dlls} "$<TARGET_FILE_DIR:${target}>"
			COMMAND_EXPAND_LISTS
		)
	endif ()

	add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
endfunction()

foreach(component IN LISTS riwo_components)
	if (NOT Riwo_${component}_FOUND)
		message(FATAL_ERROR "Component '${component}' was not reported as found.")
	endif ()

	if (NOT TARGET Riwo::${component})
		message(FATAL_ERROR "Missing imported target Riwo::${component}.")
	endif ()

	if (NOT TARGET riwo.${component})
		message(FATAL_ERROR "Missing compatibility target riwo.${component}.")
	endif ()

	string(TOUPPER "${component}" component_upper)
	set(target riwo_consumer_${component})

	add_executable(${target} main.cpp)
	target_compile_features(${target} PRIVATE cxx_std_20)

	target_compile_definitions(${target} PRIVATE
		"RIWO_CONSUME_${component_upper}=1"
	)
	target_link_libraries(${target} PRIVATE Riwo::${component})
	add_riwo_consumer_runtime_test(${target})

	list(APPEND riwo_source_targets riwo.${component})
	list(APPEND riwo_expected_definitions
		"RIWO_CONSUME_${component_upper}=1"
	)
endforeach()

add_executable(riwo_consumer_source_targets main.cpp)
target_compile_features(riwo_consumer_source_targets PRIVATE cxx_std_20)

target_compile_definitions(riwo_consumer_source_targets PRIVATE
	${riwo_expected_definitions}
	RIWO_CONSUME_UMBRELLA=1
)
target_link_libraries(riwo_consumer_source_targets PRIVATE ${riwo_source_targets})
add_riwo_consumer_runtime_test(riwo_consumer_source_targets)
