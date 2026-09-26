# SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

set(target_name riwo.coro)
add_project(${target_name} riwo.core)

target_compile_definitions (
	${target_name} PRIVATE RIWO_CORO_SHARED
)
