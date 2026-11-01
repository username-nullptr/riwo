# SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(target_name riwo.coro)
add_project(${target_name} riwo.core)

target_compile_definitions (
	${target_name} PRIVATE RIWO_CORO_SHARED
)
install(FILES ../coro.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo
)
install(DIRECTORY ./
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/riwo/coro
	FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.ipp"
)
