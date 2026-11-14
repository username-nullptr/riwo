// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "user.h"

namespace riwo::system
{

sys_expected<std::filesystem::path> expand_user_path(const std::filesystem::path &value) noexcept
{
	const auto &text = value.native();
	using char_type = std::filesystem::path::value_type;
	if( text.empty() or text.front() != static_cast<char_type>('~') )
		return value;
	if( text.size() > 1 and text[1] != static_cast<char_type>('/') and
		text[1] != static_cast<char_type>('\\') )
		return value;

	return home_directory().transform([&](const auto &home) {
		if( text.size() <= 2 )
			return home;
		return (home / std::filesystem::path(text.substr(2))).lexically_normal();
	});
}

} //namespace riwo::system
