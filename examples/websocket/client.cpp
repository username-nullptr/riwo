// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/client.h>
#include <iostream>

namespace ws = riwo::websocket;

int main(int argc, const char *argv[])
{
	const std::string endpoint = argc > 1 ?
		argv[1] : "ws://127.0.0.1:8080/echo";

	ws::client client;
	riwo::dispatch([&client, endpoint]() -> riwo::awaitable<void>
	{
		try {
			auto stream = co_await client.open (
				ws::connect_request(endpoint), riwo::use_awaitable
			);
			co_await stream.write_text("hello", riwo::use_awaitable);
			auto message = co_await stream.read<std::string>(riwo::use_awaitable);

			std::cout << message.body << '\n';
			co_await stream.close(riwo::use_awaitable);
		}
		catch(const std::exception &exception)
		{
			std::cerr << "WebSocket client failed: " << exception.what() << '\n';
			riwo::exit(1);
			co_return ;
		}
		riwo::exit();
		co_return ;
	});
	return riwo::exec();
}
