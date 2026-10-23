// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::http_exp;

int main(int argc, const char *argv[])
{
	const std::string url = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080/hello/Riwo";

	http::client client;
	riwo::error_code error;

	auto reply = client.get(url, error);
	if( error )
	{
		std::cerr << "Request failed: " << error.message() << '\n';
		return 1;
	}
	auto body = reply.read_all(error);
	if( error )
	{
		std::cerr << "Body read failed: " << error.message() << '\n';
		return 1;
	}
	std::cout << "HTTP status: " << reply.status().code() << '\n';
	std::cout << http::example::body_text(body);
	return 0;
}
