// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/client.h>
#include <iostream>
#include <string>

int main(int argc, const char *argv[])
{
	const std::string url = argc > 1 ?
		argv[1] : "http://127.0.0.1:8080/hello/Riwo";

	riwo::http::client client;
	riwo::dispatch([&client, url]() -> riwo::awaitable<void>
	{
		try {
			auto context = co_await client.request_get(url, riwo::use_awaitable);
			auto status = co_await context->wait_reply(riwo::use_awaitable);

			auto body = co_await context->reply()->read<std::string>(
				riwo::use_awaitable
			);
			std::cout << "HTTP status: " << static_cast<unsigned>(status) << '\n';
			std::cout << body;
		}
		catch(const std::exception &exception)
		{
			std::cerr << "HTTP request failed: " << exception.what() << '\n';
			riwo::exit(1);
			co_return ;
		}
		riwo::exit();
		co_return ;
	});
	return riwo::exec();
}
