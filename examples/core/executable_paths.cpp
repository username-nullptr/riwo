// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/executable.h>
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
	print_result("Executable", riwo::executable::path());
	print_result("Executable directory", riwo::executable::directory());
	print_result("Resolved path", riwo::executable::resolve("config/app.ini"));
	return 0;
}
