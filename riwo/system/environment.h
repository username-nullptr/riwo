// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_SYSTEM_ENVIRONMENT_H
#define RIWO_SYSTEM_ENVIRONMENT_H

#include <riwo/system/global.h>
#include <riwo/core/sys_expected.h>
#include <riwo/core/optional.h>
#include <map>

namespace riwo::system::environment
{

using value_t = std::string;
using values_t = std::map<std::string,value_t>;

[[nodiscard]] RIWO_SYSTEM_API
sys_expected<optional<value_t>> get(std::string_view name) noexcept;

RIWO_SYSTEM_API sys_expected<> set (
	std::string_view name, std::string_view value, bool overwrite = true
) noexcept;

RIWO_SYSTEM_API sys_expected<> unset(std::string_view name) noexcept;
[[nodiscard]] RIWO_SYSTEM_API sys_expected<values_t> all() noexcept;

} //namespace riwo::system::environment


#endif //RIWO_SYSTEM_ENVIRONMENT_H
