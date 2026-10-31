// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::experimental::http;

int main(int argc, const char *argv[])
try
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8082
	);
	http::server service;
	service
	.bind({riwo::ip_type::v4, port})

	.on_request<http::method::get>("/session",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		auto session = exchange.session();
		const auto visits = session->attribute_as<int>("visits").value_or(0) + 1;

		session
		->set_attribute("example", std::string("active"))
		 .set_attribute("visits", visits);

		http::response_head head;
		head.headers.add("Content-Type", "text/plain; charset=utf-8");

		co_await exchange.respond(std::move(head),
			std::format("Session ID: {}\nVisits: {}\n", session->id(), visits),
			riwo::use_awaitable
		);
	})
	.on_server_error([](riwo::error_code error)
	{
		std::cerr << "Server error: " << error.message() << '\n';
		return true;
	})
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port << "/session\n";
	return riwo::exec();
}
catch(const std::exception &exception)
{
	std::cerr << "EXP_HTTP session server failed: " << exception.what() << '\n';
	return 1;
}
