// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/signal_slot.h>

#include <iostream>
#include <string_view>

void print_value(int value)
{
	std::cout << "free function received " << value << '\n';
}

int main()
{
	riwo::utils::signal<void(int,std::string_view)> changed;
	changed
		.connect(print_value)
		.connect([](int value, std::string_view label)
		{
			std::cout << "lambda received " << label << " = " << value << '\n';
		});

	changed(42, "answer");
	changed.disconnect(print_value);
	changed(7, "remaining slot");
	return 0;
}
