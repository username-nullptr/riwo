// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifdef __unix__

#include <riwo/system/user.h>

#include <cerrno>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace riwo::system
{

struct user_record
{
	std::string name;
	std::filesystem::path home;
};

[[nodiscard]] static sys_expected<user_record> current_user() noexcept
{
	long capacity = sysconf(_SC_GETPW_R_SIZE_MAX);
	if( capacity < 1024 )
		capacity = 1024;

	std::vector<char> buffer(static_cast<size_t>(capacity));
	for(;;)
	{
		passwd value {};
		passwd *result = nullptr;

		const auto error = getpwuid_r (
			getuid(), &value, buffer.data(), buffer.size(), &result
		);
		if( error == ERANGE )
		{
			buffer.resize(buffer.size() * 2);
			continue;
		}
		if( error != 0 )
			return error_code(std::error_code(error, std::system_category()));

		if( result == nullptr )
			return make_system_error_code(std::errc::no_such_file_or_directory);

		user_record record;
		if( value.pw_name != nullptr )
			record.name = value.pw_name;

		if( value.pw_dir != nullptr and *value.pw_dir != '\0' )
			record.home = value.pw_dir;

		else if( const auto *home = std::getenv("HOME"); home != nullptr )
			record.home = home;
		return record;
	}
}

sys_expected<std::string> current_user_name() noexcept
{
	return current_user().transform([](const user_record &value) {
		return value.name;
	});
}

sys_expected<std::filesystem::path> home_directory() noexcept
{
	return current_user().and_then([](const user_record &value) -> sys_expected<std::filesystem::path>
	{
		if( value.home.empty() )
			return make_system_error_code(std::errc::no_such_file_or_directory);
		return value.home;
	});
}

} //namespace riwo::system

#endif //__unix__
