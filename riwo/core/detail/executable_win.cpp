// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(__WINNT__) || defined(_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
# define NOMINMAX
#endif //NOMINMAX

#include <riwo/core/executable.h>
#include <Windows.h>

namespace riwo::executable
{

[[nodiscard]] static error_code sys_error()
{
	return error_code(std::error_code(
		static_cast<int>(GetLastError()), std::system_category()
	));
}

sys_expected<path_type> path() noexcept
{
	std::vector<wchar_t> buffer(MAX_PATH);
	for(;;)
	{
		SetLastError(ERROR_SUCCESS);
		const auto size = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size())
		);
		if( size == 0 )
			return sys_error();

		if( size < buffer.size() )
			return path_type(std::wstring(buffer.data(), size));

		if( buffer.size() >= 32768 )
		{
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			return sys_error();
		}
		buffer.resize(buffer.size() * 2);
	}
}

} //namespace riwo::executable

#endif //__WINNT__ || _WINDOWS
