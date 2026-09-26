// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "app_utls.h"

namespace fs = std::filesystem;

namespace riwo::app
{

sys_expected<path_t> dir_path() noexcept
{
	return file_path().transform([](const path_t &path) -> path_t
	{
		auto file_name = path.wstring();
		auto index = file_name.find_last_of(L'/');

		if( index == std::wstring::npos or index == file_name.size() - 1 )
			return L"./";
		return file_name.erase(index + 1);
	});
}

} //namespace riwo::app
