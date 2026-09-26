// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/websocket/server.h>
#include <iostream>

namespace ws = riwo::websocket;

int main(int argc, const char *argv[])
{
	const auto port = static_cast<std::uint16_t>(
		argc > 1 ? std::stoul(argv[1]) : 8080
	);
	// HTTP is the root service. A route decides whether to write an HTTP reply
	// or transfer the connection to a WebSocket stream.
	asio::ip::tcp::acceptor acceptor(riwo::get_executor());
	riwo::http::server http_server(std::move(acceptor));

	http_server
	.bind({riwo::ip_type::v4, port})

	.on_request<riwo::http::method::get>(
		"/mixed",
		[](riwo::http::server::context_t &context) -> riwo::awaitable<void>
		{
			if( not ws::is_upgrade_request(context.request()) )
			{
				constexpr std::string_view body =
					"ordinary HTTP response from /mixed\n";

				context.response().set_header("X-Riwo-Transport", "http");
				co_await context.response().write(asio::buffer(body), riwo::use_awaitable);
				co_return ;
			}
			ws::upgrade_options options;
			options.supported_subprotocols = {"riwo.example"};
			options.require_subprotocol = true;
			options.response_headers["X-Riwo-Transport"] = "websocket";

			auto upgrade_result = co_await ws::upgrade (
				context, std::move(options), asio::as_tuple(riwo::use_awaitable)
			);
			auto &[upgrade_error, accepted] = upgrade_result;
			if( upgrade_error )
			{
				std::cerr << "WebSocket upgrade failed: "
					<< upgrade_error.message() << '\n';
				co_return ;
			}
			std::cout << "WebSocket connected: "
				<< accepted.request.remote_endpoint.to_string() << '\n';

			auto message = co_await accepted.stream.read<std::string>(riwo::use_awaitable);
			const auto response = std::format("echo: {}", message.body);

			co_await accepted.stream.write_text(response, riwo::use_awaitable);
			co_await accepted.stream.close(riwo::use_awaitable);
			co_return ;
		}
	)
	.on_server_error([](riwo::error_code error)
	{
		std::cerr << "Server error: " << error.message() << '\n';
		return true;
	})
	.on_service_error([](riwo::http::server::context_t&, const std::exception &exception)
	{
		std::cerr << "Service error: " << exception.what() << '\n';
		return true;
	})
	.start();

	std::cout << "Listening on http://127.0.0.1:" << port << "/mixed\n";
	return riwo::exec();
}
