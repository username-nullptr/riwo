// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::http_exp;

int main(int argc, const char *argv[])
try
{
	const std::string base = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080";

	http::client client;
	auto reply = client.get(base + "/login");
	auto body = reply.read_all();

	std::cout << "HTTP status: " << reply.status().code() << '\n';
	std::cout << "Stored cookies: " << client.cookie_store()->size() << '\n';
	std::cout << http::example::body_text(body);
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "Cookie example failed: " << exception.what() << '\n';
	return 1;
}
