// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/system/environment.h>
#include <riwo/system/user.h>

namespace
{

void environment()
{
	constexpr std::string_view key = "RIWO_TEST_SYSTEM_ENVIRONMENT";
	constexpr std::string_view first = "available/environment-value";

	RIWO_TEST_CHECK(riwo::system::environment::set(key, first));
	RIWO_TEST_CHECK(riwo::system::environment::set(key, "second", false));

	const auto value = riwo::system::environment::get(key);
	RIWO_TEST_CHECK(value and value->has_value());
	if( value and value->has_value() )
		RIWO_TEST_CHECK_EQ(**value, first);

	const auto values = riwo::system::environment::all();
	RIWO_TEST_CHECK(values and values->contains(std::string(key)));

	RIWO_TEST_CHECK(riwo::system::environment::unset(key));
	const auto removed = riwo::system::environment::get(key);
	RIWO_TEST_CHECK(removed and not removed->has_value());
}

void user()
{
	RIWO_TEST_CHECK(riwo::system::current_user_name());
	const auto home = riwo::system::home_directory();
	RIWO_TEST_CHECK(home and not home->empty());
	const auto expanded = riwo::system::expand_user_path("~/riwo");
	RIWO_TEST_CHECK(expanded and home and
		*expanded == (*home / "riwo").lexically_normal());
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"environment", environment},
		{"user", user},
	});
}
