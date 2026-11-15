// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifdef __unix__

#include <riwo/system/environment.h>
#include <riwo/core/shared_mutex.h>

#include <cerrno>
#include <cstdlib>

// extern char **environ;

namespace riwo::system::environment
{

[[nodiscard]] static error_code sys_error(int value = errno)
{
	return { std::error_code(value, std::system_category()) };
}

static shared_mutex g_environment_mutex;

sys_expected<optional<value_t>> get(std::string_view name) noexcept
{
	const std::string key(name);
	std::shared_lock lock(g_environment_mutex);

	const auto *value = std::getenv(key.c_str());
	if( value == nullptr )
		return std::optional<value_t>{};

	return std::optional<value_t>(std::in_place, value);
}

sys_expected<values_t> all() noexcept
{
	values_t values {};
	std::shared_lock lock(g_environment_mutex);

	for(size_t index=0; environ[index]!=nullptr; ++index)
	{
		const std::string item(environ[index]);
		const auto separator = item.find('=');

		if( separator == std::string::npos )
			values.emplace(item, value_t{});
		else
			values.emplace(item.substr(0, separator), item.substr(separator + 1));
	}
	return values;
}

sys_expected<> set(std::string_view name, std::string_view value, bool overwrite) noexcept
{
	const std::string key(name);
	const std::string text(value);

	std::unique_lock lock(g_environment_mutex);
	if( ::setenv(key.c_str(), text.c_str(), overwrite ? 1 : 0) == 0 )
		return {};
	return sys_error();
}

sys_expected<> unset(std::string_view name) noexcept
{
	const std::string key(name);
	std::unique_lock lock(g_environment_mutex);

	if( ::unsetenv(key.c_str()) == 0 )
		return {};
	return sys_error();
}

} //namespace riwo::system::environment

#endif //__unix__
