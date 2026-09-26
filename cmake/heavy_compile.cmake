# SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(RIWO_HEAVY_COMPILE_JOBS_DEFAULT 0)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	set(RIWO_HEAVY_COMPILE_JOBS_DEFAULT 8)

elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(RIWO_HEAVY_COMPILE_JOBS_DEFAULT 6)
endif ()

set(RIWO_HEAVY_COMPILE_JOBS ${RIWO_HEAVY_COMPILE_JOBS_DEFAULT} CACHE STRING
	"Maximum concurrent memory-heavy HTTP/WebSocket test and example compilations; 0 disables the limit."
)
if (NOT RIWO_HEAVY_COMPILE_JOBS MATCHES "^[0-9]+$")
	message(FATAL_ERROR
		"${PRO_NAME}: RIWO_HEAVY_COMPILE_JOBS must be a non-negative integer."
	)
endif ()

option(RIWO_LOW_MEMORY_DEBUG_INFO
	"-- ${PRO_NAME}: Use reduced GCC debug information to lower compiler memory use." OFF
)
if (RIWO_LOW_MEMORY_DEBUG_INFO)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		add_compile_options(
			$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:-g1>
		)
		message(STATUS
			"${PRO_NAME}: Use reduced GCC debug information for lower compile memory."
		)
	else ()
		message(FATAL_ERROR
			"${PRO_NAME}: RIWO_LOW_MEMORY_DEBUG_INFO requires the GNU compiler."
		)
	endif ()
endif ()

if (RIWO_HEAVY_COMPILE_JOBS GREATER 0)
	message(STATUS
		"${PRO_NAME}: Limit concurrent heavy compilations to ${RIWO_HEAVY_COMPILE_JOBS}."
	)
	if (CMAKE_GENERATOR MATCHES "Ninja")
		set_property(GLOBAL APPEND PROPERTY JOB_POOLS
			riwo_heavy_compile=${RIWO_HEAVY_COMPILE_JOBS}
		)
	endif ()
endif ()


# HTTP/WebSocket tests and examples are mostly one-source executables whose
# template instantiation can still exceed 1 GiB per compiler process.
# Keep light compilation fully parallel while bounding only those known-heavy
# sources. Ninja has native compile pools; other generators use independent
# target lanes so a global -j value can remain high.
function(riwo_limit_heavy_compile target)
	if (RIWO_HEAVY_COMPILE_JOBS EQUAL 0)
		return()
	endif ()

	if (CMAKE_GENERATOR MATCHES "Ninja")
		set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE riwo_heavy_compile)
		return()
	endif ()

	get_property(riwo_heavy_index GLOBAL PROPERTY RIWO_HEAVY_COMPILE_INDEX)

	if (NOT riwo_heavy_index)
		set(riwo_heavy_index 0)
	endif ()

	math(EXPR riwo_heavy_lane
		"${riwo_heavy_index} % ${RIWO_HEAVY_COMPILE_JOBS}"
	)
	get_property(riwo_heavy_previous GLOBAL PROPERTY
		RIWO_HEAVY_COMPILE_LANE_${riwo_heavy_lane}
	)
	if (riwo_heavy_previous)
		add_dependencies(${target} ${riwo_heavy_previous})
	endif ()

	set_property(GLOBAL PROPERTY
		RIWO_HEAVY_COMPILE_LANE_${riwo_heavy_lane} ${target}
	)
	math(EXPR riwo_heavy_index "${riwo_heavy_index} + 1")
	set_property(GLOBAL PROPERTY RIWO_HEAVY_COMPILE_INDEX ${riwo_heavy_index})
endfunction()
