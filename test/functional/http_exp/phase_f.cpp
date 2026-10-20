// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/websocket_exp.h>

namespace
{

namespace http = riwo::http_exp;
namespace ws = riwo::websocket_exp;

void websocket_round_trip(bool use_http2)
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	if( use_http2 )
	{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		server_config.protocols.requirement =
			http::protocol_requirement::require_http2;
		server_config.h2.enable_extended_connect = true;
#else
		return;
#endif
	}
	else
		server_config.protocols.requirement =
			http::protocol_requirement::require_http1;

	http::server service(std::move(acceptor), server_config);
	auto handler = [](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		RIWO_TEST_CHECK(ws::is_upgrade_request(exchange));
		ws::upgrade_options options;
		options.supported_subprotocols = {"phase-f.echo"};
		options.require_subprotocol = true;
		auto accepted = co_await ws::upgrade(exchange, std::move(options),
			riwo::use_awaitable);
		auto message = co_await accepted.stream.read<std::string>(
			riwo::use_awaitable);
		co_await accepted.stream.write_text(message.body, riwo::use_awaitable);
		auto [error, trailing] = co_await accepted.stream.read<std::string>(
			asio::as_tuple(riwo::use_awaitable));
		riwo::ignore_unused(error, trailing);
	};
	service.bind({riwo::ip_type::v4, 0});
	if( use_http2 )
		service.on_request(http::method::connect, "/echo", handler);
	else
		service.on_request<http::method::get>("/echo", handler);
	service.on_server_error([](riwo::error_code error) {
		std::cerr << "phase-f server error: " << error.message() << '\n';
		return false;
	}).on_service_error([](http::server_exchange &,
		const std::exception &error) {
		std::cerr << "phase-f service error: " << error.what() << '\n';
		return false;
	}).start();

	ws::client_config client_config;
	if( use_http2 )
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		client_config.http.protocols.requirement =
			http::protocol_requirement::require_http2;
#endif
	}
	else
		client_config.http.protocols.requirement =
			http::protocol_requirement::require_http1;
	ws::client client(context, client_config);
	const auto endpoint = std::format("ws://127.0.0.1:{}/echo",
		service.acceptor().local_endpoint().port());

	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		ws::connect_request request;
		request.endpoint = endpoint;
		request.subprotocols = {"phase-f.echo"};
		ws::open_diagnostics diagnostics;
		auto stream = co_await client.open(std::move(request), diagnostics,
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(stream.negotiated_subprotocol(), "phase-f.echo");
		RIWO_TEST_CHECK(diagnostics.selected_protocol.has_value());
		RIWO_TEST_CHECK_EQ(*diagnostics.selected_protocol,
			use_http2 ? http::protocol::v2 : http::protocol::v1_1);
		co_await stream.write_text("phase-f", riwo::use_awaitable);
		auto response = co_await stream.read<std::string>(riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(response.body, "phase-f");
		auto [close_error, close_info] = co_await stream.close(
			asio::as_tuple(riwo::use_awaitable));
		RIWO_TEST_CHECK(not close_error);
		RIWO_TEST_CHECK(close_info.clean);
		service.stop();
		context.stop();
	}, asio::use_future);

	riwo::ignore_unused(context.run_for(std::chrono::seconds(10)));
	RIWO_TEST_CHECK(completed.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	completed.get();
}

void http1_upgrade() { websocket_round_trip(false); }

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
void http2_extended_connect() { websocket_round_trip(true); }
#endif

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"phase F HTTP/1 WebSocket upgrade", http1_upgrade},
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		{"phase F HTTP/2 extended CONNECT", http2_extended_connect},
#endif
	});
}
