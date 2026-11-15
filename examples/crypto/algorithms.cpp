// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/crypto.h>
#include <iostream>

int main()
{
	riwo::crypto::sha1 digest("Hello from Riwo");
	std::cout << "SHA-1: " << digest.finalize().hex() << '\n';
	std::cout << "Base64: " << riwo::crypto::base64_encode("Hello from Riwo") << '\n';
	std::cout << "UUID: " << riwo::crypto::uuid::generate().to_string() << '\n';
	return 0;
}
