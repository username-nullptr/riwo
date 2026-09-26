// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_CORE_SYSTEM_DETAIL_APP_UTILS_H
#define RIWO_CORE_SYSTEM_DETAIL_APP_UTILS_H

namespace riwo::app:: inline literals
{

inline path_t operator""_abs(const char *path, size_t len)
{
	auto expected = absolute_path(std::string(path, len));
	if( not expected )
	{
		system_error::loc_throw (
			expected.error(), R"(riwo::app::operator""_abs<char>)"
		);
	}
	return *expected;
}

inline path_t operator""_abs(const wchar_t *path, size_t len)
{
	auto expected = absolute_path(std::wstring(path, len));
	if( not expected )
	{
		system_error::loc_throw (
			expected.error(), R"(riwo::app::operator""_abs<char>)"
		);
	}
	return *expected;
}

} //namespace riwo::app::literals


#endif //RIWO_CORE_SYSTEM_DETAIL_APP_UTILS_H
