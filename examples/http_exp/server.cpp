// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp.h>
#include <algorithm>
#include <array>
#include <format>
#include <iostream>
#include <memory>

namespace
{

namespace http = riwo::http_exp;

class request_trace final : public http::middleware
{
public:
	[[nodiscard]] riwo::awaitable<bool> before(
		http::server_exchange &exchange) override
	{
		std::cout << "middleware before " << exchange.request().method.text()
			<< ' ' << exchange.request().target << '\n';
		co_return false;
	}

	[[nodiscard]] riwo::awaitable<bool> after(
		http::server_exchange &exchange) override
	{
		std::cout << "middleware after  " << exchange.request().method.text()
			<< ' ' << exchange.request().target << '\n';
		co_return false;
	}
};

[[nodiscard]] http::protocol_requirement protocol_requirement(
	std::string_view value)
{
	if( value == "auto" ) return http::protocol_requirement::automatic;
	if( value == "http1" ) return http::protocol_requirement::require_http1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "http2" ) return http::protocol_requirement::require_http2;
#endif
	throw std::invalid_argument(
		"protocol must be auto, http1"
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		" or http2"
#endif
	);
}

[[nodiscard]] riwo::awaitable<std::string> read_body(http::body_reader &reader)
{
	std::string result;
	std::array<char,4096> buffer {};
	for(;;)
	{
		const auto read = co_await reader.read_some(
			riwo::buffer(buffer), riwo::use_awaitable);
		result.append(buffer.data(), read.bytes);
		if( read.eof ) break;
	}
	co_return result;
}

} //namespace

int main(int argc, const char *argv[])
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8084);
	const auto requirement = protocol_requirement(
		argc > 2 ? std::string_view(argv[2]) : std::string_view("auto"));

	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config config;
	config.protocols.requirement = requirement;
	http::server service(std::move(acceptor), config);
	service
		.bind({riwo::ip_type::v4, port})
		.use(std::make_shared<request_trace>())
		.on_request<http::method::get>("/health",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			co_await exchange.respond(std::move(head), "healthy\n",
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/hello/{name}",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			const auto tags = exchange.query_parameters().find_all("tag");
			const auto body = std::format("hello, {} (tags: {})\n",
				exchange.path_parameter("name").value_or("anonymous"),
				tags.size());
			http::response_head head;
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			co_await exchange.respond(std::move(head), body,
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
		.on_request<http::method::get>("/visit",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto session = exchange.session();
			const auto visits =
				session->attribute_as<int>("visits").value_or(0) + 1;
			session->set_attribute("visits", visits);
			const auto body = std::format("session visits: {}\n", visits);
			http::response_head head;
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			co_await exchange.respond(std::move(head), body,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/login",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head {http::status::found, {}};
			head.headers.add("Set-Cookie",
				"welcome=http-exp; Path=/; HttpOnly; SameSite=Lax");
			head.headers.add("Location", "/whoami");
			co_await exchange.respond(std::move(head), std::string_view{},
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/whoami",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto cookies =
				http::parse_cookie_fields(exchange.request().headers);
			const bool welcome = cookies and
				std::ranges::any_of(*cookies, [](const http::cookie_pair &item)
				{
					return item.name == "welcome" and item.value == "http-exp";
				});
			auto session = exchange.session();
			const auto body = std::format(
				"redirect followed; welcome cookie: {}; session id: {}\n",
				welcome ? "yes" : "no", session->id());
			http::response_head head;
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
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		.on_request<http::method::get>("/large",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain; charset=utf-8");
			const std::string body(4096, 'R');
			co_await exchange.respond(std::move(head), body,
				riwo::use_awaitable);
		})
#endif
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

	std::cout << "Listening on http://127.0.0.1:" << port
		<< " (protocol policy: " << (argc > 2 ? argv[2] : "auto") << ")\n"
		<< "Routes: GET /health, GET /hello/{name}, POST /echo, "
		   "GET /visit, GET /login, GET /whoami, GET /stream"
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		<< ", GET /large"
#endif
		<< '\n';
	context.run();
}
