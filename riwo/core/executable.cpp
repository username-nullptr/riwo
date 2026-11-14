// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "executable.h"

namespace riwo::executable
{

sys_expected<path_t> directory() noexcept
{
	return path().transform([](const path_t &value) {
		return value.parent_path();
	});
}

sys_expected<path_t> resolve(const path_t &value) noexcept
{
	if( value.is_absolute() )
		return value.lexically_normal();

	return directory().transform([&](const path_t &base) {
		return (base / value).lexically_normal();
	});
}

} //namespace riwo::executable
