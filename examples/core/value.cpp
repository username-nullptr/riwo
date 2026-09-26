// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/value.h>

#include <iostream>

int main()
{
	riwo::value text = "42";
	riwo::value integer = 42;
	riwo::value decimal = 3.5;
	riwo::value formatted("answer = {}", integer);

	std::cout << text.to_string() << " as int: "
		<< text.to_int().value_or(0) << '\n';

	std::cout << integer.to_string() << " as string: "
		<< integer.to_string() << '\n';

	std::cout << decimal.to_string() << " as double: "
		<< decimal.to_double().value_or(0) << '\n';

	std::cout << formatted.to_string() << '\n';

	riwo::value invalid = "not-a-number";
	auto conversion = invalid.get<int>();

	std::cout << "Invalid conversion has value: "
		<< std::boolalpha << static_cast<bool>(conversion) << '\n';

	return 0;
}
