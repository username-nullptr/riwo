// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <algorithm>
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] riwo::awaitable<std::string> read_body(http::body_reader &reader)
{
	std::string body;
	std::array<char,4096> buffer {};
	for(;;)
	{
		const auto read = co_await reader.read_some (
			riwo::buffer(buffer), riwo::use_awaitable
		);
		body.append(buffer.data(), read.bytes);
		if( read.eof )
			co_return body;
	}
}

[[nodiscard]] riwo::awaitable<void> respond_text
(http::server_exchange &exchange, const std::string &body, http::status status = http::status::ok)
{
	http::response_head head {status, {}};
	head.headers.add("Content-Type", "text/plain; charset=utf-8");

	co_await exchange.respond (
		std::move(head), body, riwo::use_awaitable
	);
}

} //namespace

int main(int argc, const char *argv[])
try
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8080
	);
	http::server_config config;
	config.protocols.requirement = http::example::parse_protocol (
		argc > 2 ? argv[2] : "auto"
	);
	http::server service(config);
	service
	.bind({riwo::ip_type::v4, port})

	.on_request<http::method::get>("/hello/{name}",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		const auto tags = exchange.query_parameters().find_all("tag");
		co_await respond_text(exchange, std::format("Hello, {}! ({} tags)\n",
			exchange.path_parameter("name").value_or("anonymous"), tags.size()
		));
	})
	.on_request<http::method::post>("/echo",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		co_await respond_text(exchange,
			co_await read_body(exchange.request_body()),
			http::status::created
		);
	})
	.on_request<http::method::get>("/login",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		http::response_head head {http::status::found, {}};
		head.set_cookie ({
			.name = "welcome",
			.value = riwo::value("http-exp"),
			.path = "/",
			.same_site = http::cookie_same_site::lax,
			.http_only = true
		});
		head.headers.add("Location", "/whoami");

		co_await exchange.respond (
			std::move(head), std::string_view{}, riwo::use_awaitable
		);
	})
	.on_request<http::method::get>("/whoami",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		auto cookies = http::parse_cookie_fields(exchange.request().headers);
		const bool found = cookies and std::ranges::any_of(*cookies,
		[](const http::cookie_pair &cookie) {
			return cookie.name == "welcome" and cookie.value == "http-exp";
		});
		co_await respond_text(exchange,
			found ? "Cookie received\n" : "Cookie missing\n"
		);
	})
	.on_request<http::method::get>("/stream",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		http::response_head head;
		head.headers.add("Content-Type", "text/plain; charset=utf-8");

		co_await exchange.respond(std::move(head),
			http::body_spec::streaming({{"X-Chunk-Count"}}),
			riwo::use_awaitable
		);
		for(std::string_view chunk : {"first\n", "second\n", "third\n"})
		{
			(void)co_await exchange.response_body().write (
				riwo::buffer(chunk), riwo::use_awaitable
			);
		}
		http::fields trailers;
		trailers.add("X-Chunk-Count", "3");

		co_await exchange.response_body().finish (
			std::move(trailers), riwo::use_awaitable
		);
	})
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
	.on_request<http::method::get>("/large",
	[](http::server_exchange &exchange) -> riwo::awaitable<void> {
		co_await respond_text(exchange, std::string(4096, 'R'));
	})
#endif
	.set_default_handler(
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		co_await respond_text (
			exchange, "Not found\n", http::status::not_found
		);
	})
	.set_server_error_handler([](riwo::error_code error)
	{
		std::cerr << "Server error: " << error.message() << '\n';
		return true;
	})
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port
		<< " (" << (argc > 2 ? argv[2] : "auto") << ")\n";
	return riwo::exec();
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP server failed: " << exception.what() << '\n';
	return 1;
}
