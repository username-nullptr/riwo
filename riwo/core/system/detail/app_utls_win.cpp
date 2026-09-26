// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#if defined(__WINNT__) || defined(_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <riwo/core/system/app_utls.h>
#include <Windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#ifdef _MSC_VER
# pragma comment(lib, "shell32.lib")
#endif

namespace fs = std::filesystem;

namespace riwo::app
{

#if 0
static LPSTR convert_error_code_to_string(DWORD errc)
{
	HLOCAL local_address = nullptr;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
				  nullptr, errc, 0, reinterpret_cast<PTSTR>(&local_address), 0, nullptr);
	return reinterpret_cast<LPSTR>(local_address);
}
#endif

[[nodiscard]] static error_code sys_error()
{
	return error_code(std::error_code (
		static_cast<int>(GetLastError()), std::system_category()
	));
}

sys_expected<path_t> file_path() noexcept
{
	WCHAR buf[MAX_PATH] {0};
	auto len = GetModuleFileNameW(nullptr, buf, MAX_PATH);

	sys_expected<path_t> result {L""};
	if( len == 0 )
		result.despair(sys_error());
	else
		result = strtls::replace(std::wstring(buf,len), L"\\", L"/");
	return result;
}

sys_expected<> set_current_directory(const path_t &path) noexcept
{
	sys_expected<> result;
	auto wpath = strtls::replace(path.wstring(), L"/", L"\\");

	if( not SetCurrentDirectoryW(wpath.c_str()) )
		result.despair(sys_error());
	return result;
}

sys_expected<path_t> current_directory() noexcept
{
	const auto capacity = GetCurrentDirectoryW(0, nullptr);
	if( capacity == 0 )
		return sys_error();

	std::wstring path(capacity, L'\0');
	const auto size = GetCurrentDirectoryW(capacity, path.data());
	if( size == 0 or size >= capacity )
		return sys_error();

	path.resize(size);
	return path_t(std::move(path));
}

constexpr size_t g_max_buf_size = 4096;

sys_expected<path_t> absolute_path(const path_t &path) noexcept
{
	auto wpath = path.wstring();
	sys_expected<path_t> result {path};

	if( not is_absolute_path(path) )
	{
		result = dir_path().transform([&](const path_t &dir) -> path_t {
			return dir.wstring() + wpath;
		});
	}
	else if( wpath.starts_with(L'~') )
	{
		wchar_t tmp[g_max_buf_size] {0};
		auto len = GetEnvironmentVariableW(L"USERPROFILE", tmp, g_max_buf_size);

		if( len == 0 or len > g_max_buf_size )
			result.despair(sys_error());
		else
		{
			auto home = strtls::replace(std::wstring(tmp,len), L"\\", L"/");
			if( home.ends_with(L'/') )
				home.pop_back();
			result = home + wpath.erase(0,1);
		}
	}
	return result.transform([](const path_t &resolved_path) -> path_t
	{
		auto normalized_path = strtls::replace(resolved_path.wstring(), L"/./", L"/", false);
		return strtls::replace(std::move(normalized_path), L"//", L"/", false);
	});
}

bool is_absolute_path(const path_t &path) noexcept
{
	auto wpath = path.wstring();
	if( wpath.starts_with(L"/") )
		return true;
	else if( wpath.starts_with(L"~") )
		return wpath.size() == 1 or wpath[1] == L'/' or wpath[1] == L'\\';

	auto pos = wpath.find(L':');
	if( pos == 0 or pos == std::wstring::npos )
		return false;

	else if( pos == wpath.size() - 1 )
		return true;

	if( wpath[pos + 1] == L'/' or wpath[pos + 1] == L'\\' )
		return true;
	return false;
}

sys_expected<std::string> getenv(std::string_view key) noexcept
{
	char buf[g_max_buf_size] = "";
	auto len = GetEnvironmentVariable(key.data(), buf, g_max_buf_size);

	sys_expected<std::string> result {""};
	if( len == 0 )
		result.despair(sys_error());
	else
		result = std::string(buf,len);
	return result;
}

sys_expected<std::map<std::string,std::string>> getenvs() noexcept
{
	using envs_t = std::map<std::string,std::string>;
	sys_expected<envs_t> result {envs_t{}};

	auto buf = GetEnvironmentStrings();
	if( buf == nullptr )
		return result.despair(sys_error());

	size_t start = 0;
	for(size_t i=0; ;i++)
	{
		if( buf[i] == '=' )
		{
			auto m = i;
			while( buf[++i] != '\0' ) {}

			result.value().emplace (
				std::string(buf + start, m - start),
				std::string(buf + m + 1, i - m - 1)
			);
			start = i + 1;
		}
		else if( buf[i] == '\0' )
			break;
	}
	return result;
}

sys_expected<> setenv(std::string_view key, const riwo::value &value, bool overwrite) noexcept
{
	sys_expected<> result;
	if( (not overwrite and app::getenv(key).has_value()) or
		SetEnvironmentVariable(key.data(), value->c_str()) )
		return result;
	return result.despair(sys_error());
}

sys_expected<> unsetenv(std::string_view key) noexcept
{
	sys_expected<> result;
	if( SetEnvironmentVariable(key.data(), nullptr) )
		return result;
	return result.despair(sys_error());
}

sys_expected<std::string> current_user() noexcept
{
	char buffer[g_max_buf_size] {};
	DWORD size = g_max_buf_size;

	if( not GetUserNameA(buffer, &size) )
		return sys_error();

	if( size > 0 and buffer[size - 1] == '\0' )
		--size;
	return std::string(buffer, size);
}

sys_expected<path_t> home_directory() noexcept
{
	sys_expected<path_t> result;
	PWSTR path = nullptr;

	auto hr = SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &path);
	if( SUCCEEDED(hr) )
	{
		result.emplace(path);
		CoTaskMemFree(path);
	}
	else
		result.despair(sys_error());
	return result;
}

} //namespace riwo::app

#endif //__WINNT__ || _WINDOWS
