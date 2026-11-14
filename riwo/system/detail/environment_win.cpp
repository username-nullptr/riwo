// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(__WINNT__) || defined(_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
# define NOMINMAX
#endif //NOMINMAX

#include <riwo/system/environment.h>
#include <Windows.h>

namespace riwo::system::environment
{

[[nodiscard]] static error_code sys_error()
{
	return error_code(std::error_code (
		static_cast<int>(GetLastError()), std::system_category()
	));
}

[[nodiscard]] static std::wstring to_wstring(std::string_view value)
{
	if( value.empty() )
		return {};

	const auto size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0
	);
	if( size <= 0 )
		return std::wstring(value.begin(), value.end());

	std::wstring result(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), result.data(), size
	);
	return result;
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

sys_expected<optional<value_t>> get(std::string_view name) noexcept
{
	const auto key = to_wstring(name);
	SetLastError(ERROR_SUCCESS);

	const auto capacity = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
	if( capacity == 0 )
	{
		const auto error = GetLastError();
		if( error == ERROR_ENVVAR_NOT_FOUND )
			return std::optional<value_t>{};

		if( error == ERROR_SUCCESS )
			return std::optional<value_t>(std::in_place);
		return sys_error();
	}

	std::wstring value(capacity, L'\0');
	const auto size = GetEnvironmentVariableW(key.c_str(), value.data(), capacity);

	if( size >= capacity )
		return sys_error();

	value.resize(size);
	return std::optional<value_t>(std::in_place, to_string(value));
}

sys_expected<values_t> all() noexcept
{
	values_t values;
	auto block = GetEnvironmentStringsW();

	if( block == nullptr )
		return sys_error();

	for(const wchar_t *entry = block; *entry != L'\0'; )
	{
		const std::wstring_view item(entry);
		const auto offset = item.starts_with(L'=') ? 1U : 0U;

		const auto separator = item.find(L'=', offset);
		if( separator != std::wstring_view::npos )
		{
			values.emplace (
				to_string(item.substr(0, separator)),
				to_string(item.substr(separator + 1))
			);
		}
		entry += item.size() + 1;
	}
	FreeEnvironmentStringsW(block);
	return values;
}

sys_expected<> set(std::string_view name, std::string_view value, bool overwrite) noexcept
{
	if( not overwrite )
	{
		auto existing = get(name);
		if( not existing )
			return sys_unexpected(existing.error());

		if( existing->has_value() )
			return {};
	}
	const auto key = to_wstring(name);
	const auto text = to_wstring(value);

	if( SetEnvironmentVariableW(key.c_str(), text.c_str()) )
		return {};

	return sys_error();
}

sys_expected<> unset(std::string_view name) noexcept
{
	if( const auto key = to_wstring(name); SetEnvironmentVariableW(key.c_str(), nullptr) )
		return {};

	if( GetLastError() == ERROR_ENVVAR_NOT_FOUND )
		return {};
	return sys_error();
}

} //namespace riwo::system::environment

#endif //__WINNT__ || _WINDOWS
