# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

option(RIWO_STRICT_WARNINGS
	"-- ${PRO_NAME}: Enable strict warnings for first-party targets." ON
)
option(RIWO_WARNINGS_AS_ERRORS
	"-- ${PRO_NAME}: Treat first-party warnings as errors." ON
)

# Apply development policy directly to a first-party target.  Keeping these
# options PRIVATE prevents an installed Riwo package from imposing its warning
# policy on consumers.  Third-party targets are intentionally never passed to
# this function; their headers are exposed through SYSTEM include directories.
function(riwo_enable_strict_warnings target)
	if (NOT TARGET ${target})
		message(FATAL_ERROR
			"${PRO_NAME}: Cannot enable warnings for missing target '${target}'."
		)
	endif ()

	if (NOT RIWO_STRICT_WARNINGS)
		return()
	endif ()

	if (MSVC)
		target_compile_options(${target} PRIVATE
			/W4
			/permissive-
			/Zc:__cplusplus
			/Zc:preprocessor
			/utf-8
			/external:W0
		)
		if (RIWO_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE /WX)
		endif ()

	elseif (CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
		target_compile_options(${target} PRIVATE
			-Wall
			-Wextra
			-Wpedantic
			-Wformat=2
			-Wundef
			-Wcast-qual
			-Wconversion
			-Wsign-conversion
			-Wdouble-promotion
			-Wimplicit-fallthrough
			$<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
			$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
			$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU>>:-Wshadow=compatible-local>
		)
		if (RIWO_WARNINGS_AS_ERRORS)
			target_compile_options(${target} PRIVATE -Werror)
		endif ()
	endif ()
endfunction()
