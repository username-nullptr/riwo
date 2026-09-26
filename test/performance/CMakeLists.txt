# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

add_riwo_performance_test(lock-free-queue lock_free_queue.cpp riwo.core)
add_riwo_performance_test(core core.cpp riwo.core)
add_riwo_performance_test(mutex mutex.cpp riwo.core)

if (RIWO_BUILD_CORO)
	add_riwo_performance_test(coro coro.cpp riwo.coro)
endif ()

if (RIWO_BUILD_HTTP)
	add_riwo_performance_test(http http.cpp riwo.http)
	add_riwo_performance_test(http-protocol http_protocol.cpp riwo.http)
endif ()

if (RIWO_BUILD_WEBSOCKET)
	add_riwo_performance_test(websocket websocket.cpp riwo.websocket)
endif ()

if (RIWO_BUILD_UTILITIES)
	add_riwo_performance_test(logger logger.cpp riwo.utils)
	add_riwo_performance_test(signal-slot signal_slot.cpp riwo.utils)
	add_riwo_performance_test(sbus sbus.cpp riwo.utils)
endif ()
