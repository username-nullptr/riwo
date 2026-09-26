// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/utils/modules.h>
#include <iostream>

int main()
{
	std::cout << "Registered module graph:\n"
		<< riwo::utils::modules::sprint() << '\n';

	riwo::utils::modules::do_init();

	std::cout << "All modules initialized\n";
	return 0;
}
