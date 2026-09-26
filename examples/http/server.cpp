// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/server.h>
#include <iostream>
#include <format>

int main(int argc, const char *argv[])
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8080
	);
	constexpr std::string_view root_body =
		"Riwo HTTP example\nTry GET /hello/your-name\n";

	asio::ip::tcp::acceptor acceptor(riwo::get_executor());
	riwo::http::server server(std::move(acceptor));

	server
	.bind({riwo::ip_type::v4, port})
	.on_request<riwo::http::method::get>(
		"/",
		[root_body](riwo::http::server::context_t &context) -> riwo::awaitable<void>
		{
			co_await context.response().write (
				asio::buffer(root_body), riwo::use_awaitable
			);
			co_return;
		}
	)
	.on_request<riwo::http::method::get>(
		"/hello/{name}",
		[](riwo::http::server::context_t &context) -> riwo::awaitable<void>
		{
			auto &request = context.request();
			std::cout << riwo::http::method::string(request.method())
				<< ' ' << request.path() << '\n';

			const auto body = std::format (
				"Hello, {}!\n", request.path_arg("name")
			);
			co_await context.response().write (
				asio::buffer(body), riwo::use_awaitable
			);
			co_return;
		}
	)
	.on_request<riwo::http::method::get>(
		"/cookies/set",
		[](riwo::http::server::context_t &context) -> riwo::awaitable<void>
		{
			riwo::http::cookie cookie("stored");
			cookie.set_path("/");

			context.response().set_cookie("riwo-example", std::move(cookie));
			constexpr std::string_view body = "Cookie stored\n";

			co_await context.response().write (
				asio::buffer(body), riwo::use_awaitable
			);
			co_return;
		}
	)
	.on_request<riwo::http::method::get>(
		"/cookies/show",
		[](riwo::http::server::context_t &context) -> riwo::awaitable<void>
		{
			auto cookie = context.request().cookie("riwo-example");
			const auto body = cookie
				? std::format("Cookie received: {}\n", cookie->to_string())
				: std::string("Cookie missing\n");

			co_await context.response().write (
				asio::buffer(body), riwo::use_awaitable
			);
			co_return;
		}
	)
	.on_default([](riwo::http::server::context_t &context) -> riwo::awaitable<void>
	{
		constexpr std::string_view body = "Not found\n";
		context.response().set_status(riwo::http::status::not_found);

		co_await context.response().write (
			asio::buffer(body), riwo::use_awaitable
		);
		co_return;
	})
	.on_server_error([](std::error_code error)
	{
		std::cerr << "Server error: " << error.message() << '\n';
		return true;
	})
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port << '\n';
	return riwo::exec();
}
