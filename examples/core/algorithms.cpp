// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/algorithm.h>
#include <riwo/core/mime_type.h>
#include <iostream>

int main()
{
	std::cout << "MIME type: " << riwo::mime_type::get("index.html") << '\n';

	riwo::sha1 digest("Hello from Riwo");
	std::cout << "SHA-1: " << digest.finalize().hex() << '\n';
	std::cout << "UUID: " << riwo::uuid::generate().to_string() << '\n';

	auto weight = riwo::wildcard_match("lib*.so*", "libexample.so.1");
	std::cout << "Wildcard match weight: " << weight << '\n';
	return 0;
}
