// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/system/app_utls.h>
#include <iostream>

template <typename T>
void print_result(std::string_view label, const riwo::sys_expected<T> &result)
{
	if(result)
		std::cout << label << ": " << *result << '\n';
	else
		std::cerr << label << " failed: " << result.error().message() << '\n';
}

int main()
{
	print_result("Executable", riwo::app::file_path());
	print_result("Executable directory", riwo::app::dir_path());
	print_result("Working directory", riwo::app::current_directory());
	print_result("Absolute current directory", riwo::app::absolute_path("."));
	print_result("Current user", riwo::app::current_user());
	print_result("Home directory", riwo::app::home_directory());
	return 0;
}
