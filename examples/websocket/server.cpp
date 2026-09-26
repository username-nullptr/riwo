// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/server.h>
#include <iostream>

namespace ws = riwo::websocket;

int main(int argc, const char *argv[])
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8080);

	asio::ip::tcp::acceptor acceptor(riwo::get_executor());
	ws::server server(std::move(acceptor));
	server
	.bind({riwo::ip_type::v4, port})

	.on_connection("/echo", [](ws::accept_result accepted) -> riwo::awaitable<void>
	{
		try {
			auto message = co_await accepted.stream
				.read<std::string>(riwo::use_awaitable);

			co_await accepted.stream
				.write_text("echo: " + message.body, riwo::use_awaitable);

			co_await accepted.stream.close(riwo::use_awaitable);
		}
		catch(const std::exception &exception) {
			std::cerr << "WebSocket session failed: " << exception.what() << '\n';
		}
		co_return ;
	})
	.on_server_error([](riwo::error_code error)
	{
		std::cerr << "WebSocket server failed: " << error.message() << '\n';
		return true;
	})
	.start();

	std::cout << "Listening on ws://127.0.0.1:" << port << "/echo\n";
	return riwo::exec();
}
