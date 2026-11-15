// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/percent_encoding.h>
#include <riwo/core/wildcard.h>
#include <iostream>

int main()
{
	std::cout << "Percent encoding: "
		<< riwo::to_percent_encoding("hello world") << '\n';

	auto weight = riwo::wildcard_match("lib*.so*", "libexample.so.1");

	std::cout << "Wildcard match weight: " << weight << '\n';
	return 0;
}
