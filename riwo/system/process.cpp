// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "riwo/system/process.h"
#include <riwo/system/user.h>

namespace riwo::system::detail
{

sys_expected<uint64_t> process::set_single(std::string_view key)
{
	return system::home_directory().and_then([&](const auto &path) mutable {
		return set_single(path, key);
	});
}

} //namespace riwo::system::detail
