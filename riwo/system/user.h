// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_USER_H
#define RIWO_SYSTEM_USER_H

#include <riwo/system/global.h>
#include <riwo/core/sys_expected.h>

namespace riwo::system
{

[[nodiscard]] RIWO_SYSTEM_API
sys_expected<std::string> current_user_name() noexcept;

[[nodiscard]] RIWO_SYSTEM_API
sys_expected<std::filesystem::path> home_directory() noexcept;

[[nodiscard]] RIWO_SYSTEM_API
sys_expected<std::filesystem::path> expand_user_path(const std::filesystem::path &value) noexcept;

} //namespace riwo::system


#endif //RIWO_SYSTEM_USER_H
