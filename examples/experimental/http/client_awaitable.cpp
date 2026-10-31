// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::experimental::http;

int main(int argc, const char *argv[])
{
	const std::string url = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080/hello/Riwo";

	http::client client;
	int result = 0;

	riwo::dispatch([&]() -> riwo::awaitable<void>
	{
		try {
			auto reply = co_await client.get(url, riwo::use_awaitable);
			auto body = co_await reply.read_all(riwo::use_awaitable);

			std::cout << "HTTP status: " << reply.status().code() << '\n';
			std::cout << http::example::body_text(body);
		}
		catch(const std::exception &exception)
		{
			std::cerr << "HTTP request failed: " << exception.what() << '\n';
			result = 1;
		}
		riwo::exit(result);
	});
	riwo::io_context().run();
	return result;
}
