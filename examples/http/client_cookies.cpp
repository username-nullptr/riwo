// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/client.h>
#include <iostream>
#include <string>

int main(int argc, const char *argv[])
{
	const std::string base_url = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080";

	try {
		riwo::http::client client;

		auto set = client.request_get(base_url + "/cookies/set");
		set->reply()->read<std::string>();

		auto show = client.request_get(base_url + "/cookies/show");
		auto body = show->reply()->read<std::string>();

		std::cout << "Stored cookies: " << client.cookie_store()->size() << '\n';
		std::cout << body;
	}
	catch(const std::exception &exception)
	{
		std::cerr << "Cookie example failed: " << exception.what() << '\n';
		return 1;
	}
	return 0;
}
