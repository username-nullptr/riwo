// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/server.h>

#include <cstdint>
#include <format>
#include <iostream>

int main(int argc, const char *argv[])
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8082
	);
	asio::ip::tcp::acceptor acceptor(riwo::get_executor());
	riwo::http::server server(std::move(acceptor));

	server
	.bind({riwo::ip_type::v4, port})
	.on_request<riwo::http::method::get>(
		"/session",
		[](riwo::http::server::context_t &context) -> riwo::awaitable<void>
		{
			auto session = context.session();
			session->set_attribute("example", std::string("active"));

			const auto body = std::format (
				"Session ID: {}\n", session->id()
			);
			co_await context.response().write (
				asio::buffer(body), riwo::use_awaitable
			);
			co_return;
		}
	)
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port
		<< "/session\n";
	return riwo::exec();
}
