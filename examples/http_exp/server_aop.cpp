// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

class request_log final : public http::middleware
{
public:
	[[nodiscard]] riwo::awaitable<bool> before(
		http::server_exchange &exchange) override
	{
		std::cout << "before: " << exchange.request().target << '\n';
		co_return false;
	}

	[[nodiscard]] riwo::awaitable<bool> after(
		http::server_exchange &exchange) override
	{
		std::cout << "after: " << exchange.request().target << '\n';
		co_return false;
	}
};

class greeting_controller final : public http::controller
{
public:
	[[nodiscard]] riwo::awaitable<void> service(
		http::server_exchange &exchange) override
	{
		http::response_head head;
		head.headers.add("Content-Type", "text/plain; charset=utf-8");
		co_await exchange.respond(std::move(head),
			"Hello from a controller\n", riwo::use_awaitable);
	}
};

} //namespace

int main(int argc, const char *argv[])
try
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8081);

	riwo::io_context_t context;
	http::server service(context);
	service
		.bind({riwo::ip_type::v4, port})
		.on_request<http::method::get>("/hello",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			co_await exchange.respond(std::move(head),
				"Hello through middleware\n", riwo::use_awaitable);
		}, std::make_shared<request_log>())
		.on_request<http::method::get>(
			"/controller", std::make_shared<greeting_controller>())
		.set_server_error_handler([](riwo::error_code error)
		{
			std::cerr << "Server error: " << error.message() << '\n';
			return true;
		})
		.start();

	std::cout << "Listening on http://127.0.0.1:" << port << '\n';
	context.run();
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP AOP server failed: " << exception.what() << '\n';
	return 1;
}
