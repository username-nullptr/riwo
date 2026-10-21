// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket_exp.h>
#include <iostream>

namespace
{

namespace http = riwo::http_exp;
namespace ws = riwo::websocket_exp;

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

} //namespace

int main(int argc, const char *argv[])
try
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8085);
	const std::string_view policy = argc > 2 ? argv[2] : "auto";

	riwo::io_context_t context;
	http::server_config config;
	config.protocols.requirement = protocol_requirement(policy);
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	config.h2.enable_extended_connect = true;
#endif
	asio::ip::tcp::acceptor acceptor(context);
	http::server_t service(std::move(acceptor), config);
	auto echo = [](http::server_exchange_t &exchange) -> riwo::awaitable<void>
	{
		if( not ws::is_upgrade_request(exchange) )
		{
			co_await exchange.respond({http::status::bad_request, {}},
				"WebSocket upgrade required\n", riwo::use_awaitable);
			co_return;
		}

		ws::upgrade_options options;
		options.supported_subprotocols = {"riwo.echo.v1"};
		options.require_subprotocol = true;
#if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
		options.supported_extensions.push_back(
			ws::permessage_deflate_extension());
#endif
		auto accepted = co_await ws::upgrade(exchange, std::move(options),
			riwo::use_awaitable);
		for(;;)
		{
			auto [error, message] = co_await accepted.stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			if( error ) break;
			if( message.type == ws::message_type::text )
				co_await accepted.stream.write_text(message.body,
					riwo::use_awaitable);
			else
				co_await accepted.stream.write_binary(
					riwo::buffer(message.body), riwo::use_awaitable);
		}
	};

	service.bind({riwo::ip_type::v4, port});
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	service.on_request<http::method::get,http::method::connect>("/echo", echo);
#else
	service.on_request<http::method::get>("/echo", echo);
#endif
	service
		.on_default(
		[](http::server_exchange_t &exchange) -> riwo::awaitable<void>
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

	std::cout << "WebSocket_EXP echo server: ws://127.0.0.1:" << port
		<< "/echo (protocol policy: " << policy << ")\n";
	context.run();
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "WebSocket_EXP server failed: " << exception.what() << '\n';
	return 1;
}
