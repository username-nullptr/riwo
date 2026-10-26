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
	if( value == "http1" ) return http::protocol_requirement::v1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "http2" ) return http::protocol_requirement::v2;
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
	ws::server_config config;
	config.http.protocols.requirement = protocol_requirement(policy);
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor), config);
	auto echo = [](ws::accept_result accepted) -> riwo::awaitable<void>
	{
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
	ws::upgrade_options upgrade;
	upgrade.supported_subprotocols = {"riwo.echo.v1"};
	upgrade.require_subprotocol = true;
#if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
	upgrade.supported_extensions.push_back(
		ws::permessage_deflate_extension());
#endif

	service.bind({riwo::ip_type::v4, port});
	service.on_connection("/echo", std::move(echo), std::move(upgrade));
	service
		.on_default(
		[](ws::accept_result accepted) -> riwo::awaitable<void>
		{
			co_await accepted.stream.close(ws::close_frame(
				ws::close_code::policy_violation, "unknown WebSocket route"),
				riwo::use_awaitable);
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
