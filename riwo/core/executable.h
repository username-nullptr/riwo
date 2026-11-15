// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_EXECUTABLE_H
#define RIWO_CORE_EXECUTABLE_H

#include <riwo/core/sys_expected.h>

namespace riwo::executable
{

using path_t = std::filesystem::path;

[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> path() noexcept;

[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> directory() noexcept;

/**
 * Resolve a path relative to the directory containing the running executable.
 * Absolute paths keep their absolute semantics. The result is normalized
 * lexically and the referenced file does not need to exist.
 */
[[nodiscard]] RIWO_CORE_API
sys_expected<path_t> resolve(const path_t &value) noexcept;

} //namespace riwo::executable


#endif //RIWO_CORE_EXECUTABLE_H
