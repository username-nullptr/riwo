// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp.h>
#include <array>
#include <iostream>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] riwo::awaitable<std::string> read_body(http::body_reader &reader)
{
	std::string result;
	std::array<char,4096> buffer {};
	for(;;)
	{
		try {
			const auto size = co_await reader.read_some(
				riwo::buffer(buffer), riwo::use_awaitable);
			result.append(buffer.data(), size);
		}
		catch(const std::system_error &exception)
		{
			if( exception.code() ==
				asio::error::make_error_code(asio::error::eof) ) break;
			throw;
		}
	}
	co_return result;
}

} //namespace

int main(int argc, const char *argv[])
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8084);

	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	service
		.bind({riwo::ip_type::v4, port})
		.on_request<http::method::get>("/health",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			co_await exchange.respond(std::move(head), "healthy\n",
				riwo::use_awaitable);
		})
		.on_request<http::method::post>("/echo",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto body = co_await read_body(exchange.request_body());
			http::response_head head {http::status::created, {}};
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			co_await exchange.respond(std::move(head), body,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/stream",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			co_await exchange.respond(std::move(head),
				http::body_spec::streaming({{"X-Chunk-Count"}}),
				riwo::use_awaitable);
			for( const std::string_view chunk : {"first\n", "second\n", "third\n"} )
			{
				(void)co_await exchange.response_body().write(
					riwo::buffer(chunk), riwo::use_awaitable);
			}
			http::fields trailers;
			trailers.add("X-Chunk-Count", "3");
			co_await exchange.response_body().finish(
				std::move(trailers), riwo::use_awaitable);
		})
		.on_default(
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::not_found, {}},
				"not found\n", riwo::use_awaitable);
		})
		.on_server_error([](riwo::error_code error)
		{
			std::cerr << "Server error: " << error.message() << '\n';
			return true;
		})
		.start();

	std::cout << "Listening on http://127.0.0.1:" << port << '\n'
		<< "Routes: GET /health, POST /echo, GET /stream\n";
	context.run();
}
