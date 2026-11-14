// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(__WINNT__) || defined(_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
# define NOMINMAX
#endif //NOMINMAX

#include <riwo/system/user.h>
#include <riwo/system/environment.h>

#include <Windows.h>
#include <knownfolders.h>

#include <lmcons.h>
#include <shlobj.h>

namespace riwo::system
{

[[nodiscard]] static error_code sys_error(DWORD value = GetLastError())
{
	return error_code(std::error_code (
		static_cast<int>(value), std::system_category()
	));
}

[[nodiscard]] static std::string to_string(std::wstring_view value)
{
	if( value.empty() )
		return {};

	const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr
	);
	if( size <= 0 )
	{
		std::string result;
		result.reserve(value.size());

		for(const auto character : value)
			result.push_back(static_cast<char>(character));
		return result;
	}
	std::string result(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), result.data(), size, nullptr, nullptr
	);
	return result;
}

sys_expected<std::string> current_user_name() noexcept
{
	std::wstring buffer(UNLEN + 1, L'\0');
	auto size = static_cast<DWORD>(buffer.size());

	if( not GetUserNameW(buffer.data(), &size) )
		return sys_error();

	if( size > 0 and buffer[size - 1] == L'\0' )
		--size;

	buffer.resize(size);
	return to_string(buffer);
}

sys_expected<std::filesystem::path> home_directory() noexcept
{
	PWSTR value = nullptr;
	const auto result = SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &value);

	if( SUCCEEDED(result) and value != nullptr )
	{
		std::filesystem::path path(value);
		CoTaskMemFree(value);

		if( not path.empty() )
			return path;
	}
	if( auto profile = environment::get("USERPROFILE");
		profile and profile->has_value() and not profile->value().empty() )
	{
		return std::filesystem::path(**profile);
	}
	auto drive = environment::get("HOMEDRIVE");
	auto path = environment::get("HOMEPATH");

	if( drive and path and drive->has_value() and path->has_value() )
		return std::filesystem::path(**drive + **path);

	const auto error = FAILED(result) ?
		HRESULT_CODE(result) : ERROR_PATH_NOT_FOUND;

	return sys_error(error == ERROR_SUCCESS ?
		ERROR_PATH_NOT_FOUND : error
	);
}

} //namespace riwo::system

#endif //__WINNT__ || _WINDOWS
