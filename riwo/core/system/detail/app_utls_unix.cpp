// SPDX-FileCopyrightText: 2024-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifdef __unix__

#include "riwo/core/system/app_utls.h"
#include "riwo/core/shared_mutex.h"

#include <unistd.h>
#include <pwd.h>

/* extern char **environ; */

namespace fs = std::filesystem;

namespace riwo::app
{

[[nodiscard]] static error_code sys_error()
{
	return error_code(std::error_code(errno, std::system_category()));
}

sys_expected<path_t> file_path() noexcept
{
	sys_expected<path_t> result {""};
	char exe_name[1024] = "";

	if( readlink("/proc/self/exe", exe_name, sizeof(exe_name)) < 0 )
		result.despair(sys_error());
	else
		result = exe_name;
	return result;
}

sys_expected<> set_current_directory(const path_t &path) noexcept
{
	sys_expected<> result;
	auto str = path.string();

	if( chdir(str.data()) < 0 )
		result.despair(sys_error());
	return result;
}

sys_expected<path_t> current_directory() noexcept
{
	sys_expected<path_t> result {""};
	char buf[1024] = "";

	if( getcwd(buf, sizeof(buf)) == nullptr )
		result.despair(sys_error());
	else
		result = path_t(buf);
	return result;
}

sys_expected<path_t> absolute_path(const path_t &path) noexcept
{
	auto str = path.string();
	sys_expected<path_t> result = path;

	if( not is_absolute_path(path) )
	{
		result = dir_path().transform([&](const path_t &dir) -> path_t {
			return dir.string() + str;
		});
	}
	else if( str.starts_with('~') )
	{
		result = home_directory().transform([&](const path_t &_path) -> path_t {
			return _path.string() + str.erase(0,1);
		});
	}
	return result.transform([](const path_t &resolved_path) -> path_t
	{
		auto normalized_path = strtls::replace(resolved_path.string(), "/./", "/", false);
		return strtls::replace(std::move(normalized_path), "//", "/", false);
	});
}

bool is_absolute_path(const path_t &path) noexcept
{
	auto str = path.string();
	if( str.starts_with('/') )
		return true;

	else if( str.starts_with('~') )
		return str.size() == 1 or str[1] == '/';
	return false;
}

// libc environment access can allocate and walk the complete environment.
// This is not a bounded low-latency critical section.
static shared_mutex g_env_mutex;

sys_expected<std::string> getenv(std::string_view key) noexcept
{
	std::shared_lock lock(g_env_mutex);
	auto value = ::getenv(key.data());

	sys_expected<std::string> result {""};
	if( value )
		result = value;
	else
		result.despair(sys_error());
	return result;
}

sys_expected<std::map<std::string,std::string>> getenvs() noexcept
{
	std::map<std::string,std::string> envs;
	std::shared_lock lock(g_env_mutex);

	for(int i=0; environ[i]!=nullptr; i++)
	{
		std::string tmp = environ[i];
		auto pos = tmp.find('=');

		if( pos == std::string::npos )
			envs.emplace(tmp, "");
		else
			envs.emplace(tmp.substr(0,pos), tmp.substr(pos+1));
	}
	return envs;
}

sys_expected<> setenv(std::string_view key, const riwo::value &value, bool overwrite) noexcept
{
	sys_expected<> result;
	std::unique_lock locker(g_env_mutex);

	if( ::setenv(key.data(), value->c_str(), overwrite) != 0 )
		result.despair(sys_error());
	return result;
}

sys_expected<> unsetenv(std::string_view key) noexcept
{
	sys_expected<> result;
	std::unique_lock locker(g_env_mutex);

	if( ::unsetenv(key.data()) != 0 )
		result.despair(sys_error());
	return result;
}

sys_expected<std::string> current_user() noexcept
{
	auto uid = getuid();
	passwd pwd {};
	passwd *result = nullptr;
	char buf[1024] {0};

	auto res = getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);
	sys_expected<std::string> expected {};

	if( res != 0 or not result )
		return expected.despair(sys_error());

	expected = pwd.pw_name;
	return expected;
}

sys_expected<path_t> home_directory() noexcept
{
	auto uid = getuid();
	passwd pwd {};
	passwd *result = nullptr;
	char buf[1024] {0};

	auto res = getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);
	sys_expected<path_t> expected {};

	if( res != 0 or not result )
		return expected.despair(sys_error());

	std::string path {};
	if( pwd.pw_dir and strlen(pwd.pw_dir) > 0 )
		path = pwd.pw_dir;
	else
	{
		auto home = ::getenv("HOME");
		if( home and strlen(home) > 0 )
			path = home;
		else
			return expected.despair(sys_error());
	}
	if( path.ends_with('/') )
		path.pop_back();

	expected = std::move(path);
	return expected;
}

} //namespace riwo::app

#endif //__unix__
