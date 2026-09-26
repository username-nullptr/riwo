# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

function(add_riwo_fuzzer module name source library)
	cmake_parse_arguments(FUZZ "" "CORPUS;DICTIONARY" "" ${ARGN})
	set(target riwo.fuzz.${module}.${name})

	add_executable(${target} ${source})
	target_link_libraries(${target} PRIVATE ${library})

	if (RIWO_FUZZER_RUNTIME_LIBRARY)
		target_compile_options(${target} PRIVATE
			-fsanitize=fuzzer-no-link,address,undefined
			-fno-omit-frame-pointer
		)
		target_link_options(${target} PRIVATE
			-fsanitize=fuzzer-no-link,address,undefined
		)
		target_link_libraries(${target} PRIVATE
			${RIWO_FUZZER_RUNTIME_LIBRARY} -Wl,-lstdc++
		)
	else ()
		target_compile_options(${target} PRIVATE
			-fsanitize=fuzzer,address,undefined
			-fno-omit-frame-pointer
		)
		target_link_options(${target} PRIVATE
			-fsanitize=fuzzer,address,undefined
		)
	endif ()

	target_compile_definitions(${target} PRIVATE
		RIWO_FUZZ_MAX_LENGTH=${RIWO_FUZZ_MAX_LENGTH}
	)
	set_target_properties(${target} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY ${RIWO_OUTPUT_DIR}/fuzz
	)
	set(fuzzer_artifacts ${CMAKE_CURRENT_BINARY_DIR}/artifacts/${module}-${name})
	file(MAKE_DIRECTORY ${fuzzer_artifacts})

	set(fuzzer_arguments
		-runs=${RIWO_FUZZ_SMOKE_RUNS}
		-seed=${RIWO_FUZZ_SEED}
		-max_len=${RIWO_FUZZ_MAX_LENGTH}
		-rss_limit_mb=${RIWO_FUZZ_RSS_LIMIT_MB}
		-timeout=${RIWO_FUZZ_TIMEOUT}
		-use_value_profile=1
		-print_final_stats=1
		-artifact_prefix=${fuzzer_artifacts}/
	)
	if (FUZZ_CORPUS)
		set(fuzzer_corpus ${CMAKE_CURRENT_BINARY_DIR}/corpus/${module}-${name})
		file(MAKE_DIRECTORY ${fuzzer_corpus})

		file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/${FUZZ_CORPUS}/
			DESTINATION ${fuzzer_corpus}
		)
		list(APPEND fuzzer_arguments ${fuzzer_corpus})
	endif ()

	if (FUZZ_DICTIONARY)
		list(APPEND fuzzer_arguments
			-dict=${CMAKE_CURRENT_SOURCE_DIR}/${FUZZ_DICTIONARY})
	endif ()

	add_riwo_ctest(${target} $<TARGET_FILE:${target}> ${fuzzer_arguments})
	set_tests_properties(${target} PROPERTIES
		LABELS "fuzz;sanitizer"
		RUN_SERIAL TRUE
		TIMEOUT 60
		ENVIRONMENT
			"ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
	)
endfunction()

foreach(target riwo.core riwo.coro riwo.http riwo.websocket riwo.utils)
	if (TARGET ${target})
		target_compile_options(${target} PRIVATE
			-fsanitize=fuzzer-no-link,address,undefined
			-fno-omit-frame-pointer
		)
		target_link_options(${target} PRIVATE -fsanitize=address,undefined)
	endif ()
endforeach()

add_riwo_fuzzer(core url core_url.cpp riwo.core
	CORPUS corpus/core DICTIONARY dictionaries/core.dict
)
add_riwo_fuzzer(core public-api core_public_api.cpp riwo.core
	CORPUS corpus/core DICTIONARY dictionaries/core.dict
)
add_riwo_fuzzer(core queue core_queue.cpp riwo.core
	CORPUS corpus/sequences DICTIONARY dictionaries/sequences.dict
)
if (RIWO_BUILD_CORO)
	add_riwo_fuzzer(coro sync-primitives coro_sync_primitives.cpp riwo.coro
		CORPUS corpus/sequences DICTIONARY dictionaries/sequences.dict
	)
endif ()

if (RIWO_BUILD_HTTP)
	add_riwo_fuzzer(http request-parser http_request_parser.cpp riwo.http
		CORPUS corpus/http-request DICTIONARY dictionaries/http.dict
	)
	add_riwo_fuzzer(http response-parser http_response_parser.cpp riwo.http
		CORPUS corpus/http-response DICTIONARY dictionaries/http.dict
	)
	add_riwo_fuzzer(http roundtrip http_roundtrip.cpp riwo.http
		CORPUS corpus/http-values DICTIONARY dictionaries/http.dict
	)
	add_riwo_fuzzer(http public-api http_public_api.cpp riwo.http
		CORPUS corpus/http-values DICTIONARY dictionaries/http.dict
	)
endif ()

if (RIWO_BUILD_WEBSOCKET)
	add_riwo_fuzzer(websocket frame-parser websocket_frame_parser.cpp riwo.websocket
		CORPUS corpus/websocket-frame DICTIONARY dictionaries/websocket.dict
	)
	add_riwo_fuzzer(websocket handshake-parser websocket_handshake_parser.cpp riwo.websocket
		CORPUS corpus/websocket-handshake DICTIONARY dictionaries/websocket.dict
	)
	add_riwo_fuzzer(websocket public-api websocket_public_api.cpp riwo.websocket
		CORPUS corpus/websocket-values DICTIONARY dictionaries/websocket.dict
	)
endif ()

if (RIWO_BUILD_UTILITIES)
	add_riwo_fuzzer(utils signal-sequence utils_signal_sequence.cpp riwo.utils
		CORPUS corpus/sequences DICTIONARY dictionaries/sequences.dict
	)
endif ()
