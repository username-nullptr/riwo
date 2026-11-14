// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/system/environment.h>
#include <riwo/system/user.h>
#include <iostream>

int main()
{
	if( auto user = riwo::system::current_user_name(); user )
		std::cout << "Current user: " << *user << '\n';

	if( auto home = riwo::system::home_directory(); home )
		std::cout << "Home directory: " << *home << '\n';

	constexpr auto name = "RIWO_SYSTEM_EXAMPLE";
	if( auto result = riwo::system::environment::set(name, "available"); not result )
	{
		std::cerr << "Set environment variable failed: "
			<< result.error().message() << '\n';
		return 1;
	}
	if( auto value = riwo::system::environment::get(name);
		value and value->has_value() )
	{
		std::cout << name << ": " << **value << '\n';
	}
	riwo::ignore_unused(riwo::system::environment::unset(name));
	return 0;
}
