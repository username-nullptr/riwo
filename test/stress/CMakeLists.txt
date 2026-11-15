# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

add_riwo_stress_test(core core.cpp riwo.core)
add_riwo_stress_test(core.jthread jthread.cpp riwo.core)
# Stress only the Riwo fallback, never the native standard-library type.
target_compile_definitions(riwo.test.stress.core.jthread PRIVATE
	RIWO_HAS_STD_JTHREAD=0
)

if (RIWO_BUILD_CORO)
	add_riwo_stress_test(coro coro.cpp riwo.coro)
endif ()

if (RIWO_BUILD_HTTP)
	add_riwo_stress_test(http http.cpp riwo.http)
endif ()

if (RIWO_BUILD_WEBSOCKET)
	add_riwo_stress_test(websocket websocket.cpp riwo.websocket)
endif ()

if (RIWO_BUILD_UTILITIES)
	add_riwo_stress_test(utils utils.cpp riwo.utils)
	if (RIWO_BUILD_UTILITIES_SBUS_UDP)
		add_riwo_stress_test(utils.sbus_udp sbus_udp.cpp riwo.utils)
	endif ()
endif ()
