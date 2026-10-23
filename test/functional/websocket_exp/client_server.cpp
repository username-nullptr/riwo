// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/websocket_exp.h>

#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/steady_timer.hpp>
#include <asio/write.hpp>

#include <memory>
#include <string>
#include <thread>

namespace
{

namespace http = riwo::http_exp;
namespace ws = riwo::websocket_exp;

void websocket_round_trip(bool use_http2, bool compression)
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config server_config;
	if( use_http2 )
	{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		server_config.http.protocols.requirement =
			http::protocol_requirement::require_http2;
#else
		return;
#endif
	}
	else
		server_config.http.protocols.requirement =
			http::protocol_requirement::require_http1;

	ws::server service(std::move(acceptor), server_config);
	auto handler = [](ws::accept_result accepted)
		-> riwo::awaitable<void>
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
	ws::upgrade_options options;
	riwo::optional<ws::request_info> validated_request;
	options.supported_subprotocols = {"riwo.echo.v1"};
	options.require_subprotocol = true;
	options.async_request_validator =
	[&validated_request](const ws::request_info &request) -> riwo::awaitable<
		ws::upgrade_validation_result>
	{
		validated_request = request;
		co_return riwo::nullopt;
	};
	options.async_subprotocol_selector =
	[](const ws::request_info &,
		std::span<const std::string> offered)
		-> riwo::awaitable<riwo::optional<std::string>>
	{
		co_return offered.empty() ? riwo::nullopt :
			riwo::optional<std::string>(offered.front());
	};
#if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
	if( compression )
		options.supported_extensions.push_back(
			ws::permessage_deflate_extension());
#else
	riwo::ignore_unused(compression);
#endif
	service.bind({riwo::ip_type::v4, 0});
	service.on_connection("/echo", std::move(handler), std::move(options));
	service.on_server_error([](riwo::error_code error) {
		std::cerr << "WebSocket_EXP server error: " << error.message() << '\n';
		return false;
	}).on_service_error([](ws::server::context_t &,
		const std::exception &error) {
		std::cerr << "WebSocket_EXP service error: " << error.what() << '\n';
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
	const auto endpoint = std::format("ws://127.0.0.1:{}/echo?case=roundtrip",
		service.http_server().acceptor().local_endpoint().port());

	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		ws::connect_request request;
		request.endpoint = endpoint;
		request.subprotocols = {"riwo.echo.v1"};
#if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
	if( compression )
		request.extensions.push_back(ws::permessage_deflate_extension());
#endif
		ws::open_diagnostics diagnostics;
		auto stream = co_await client.open(std::move(request), diagnostics,
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(stream.negotiated_subprotocol(), "riwo.echo.v1");
		RIWO_TEST_CHECK_EQ(!stream.negotiated_extensions().empty(),
			compression);
		RIWO_TEST_CHECK(diagnostics.selected_protocol.has_value());
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		RIWO_TEST_CHECK_EQ(*diagnostics.selected_protocol,
			use_http2 ? http::protocol::v2 : http::protocol::v1_1);
#else
		RIWO_TEST_CHECK(not use_http2);
		RIWO_TEST_CHECK_EQ(*diagnostics.selected_protocol,
			http::protocol::v1_1);
#endif
		struct overlap_state
		{
			std::string body {};
			riwo::error_code error {};
			bool done = false;
		};
		auto overlap = std::make_shared<overlap_state>();
		const std::string first = "websocket-exp-overlap";
		asio::co_spawn(context,
		[&stream, overlap]() -> riwo::awaitable<void>
		{
			auto [error, response] = co_await stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			overlap->error = error;
			if( not error ) overlap->body = response.body;
			overlap->done = true;
		}, asio::detached);
		co_await stream.write_text(first, riwo::use_awaitable);
		while( not overlap->done )
		{
			asio::steady_timer timer(context);
			timer.expires_after(std::chrono::milliseconds(1));
			co_await timer.async_wait(riwo::use_awaitable);
		}
		RIWO_TEST_CHECK(not overlap->error);
		RIWO_TEST_CHECK_EQ(overlap->body, first);
		for( int index = 0; index < 2; ++index )
		{
			const auto payload = std::format("websocket-exp-{}", index);
			co_await stream.write_text(payload, riwo::use_awaitable);
			auto response = co_await stream.read<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(response.body, payload);
		}
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
	RIWO_TEST_CHECK(validated_request.has_value());
	RIWO_TEST_CHECK_EQ(validated_request->path, "/echo");
	RIWO_TEST_CHECK(validated_request->target.starts_with("/echo?"));
	auto query = validated_request->query_parameters.find("case");
	RIWO_TEST_CHECK(query != validated_request->query_parameters.end());
	RIWO_TEST_CHECK_EQ(query->second.to_string(), "roundtrip");
	RIWO_TEST_CHECK(not validated_request->remote_address.empty());
	RIWO_TEST_CHECK(validated_request->remote_port != 0);
	RIWO_TEST_CHECK(validated_request->remote_endpoint.port() != 0);
	RIWO_TEST_CHECK(validated_request->local_endpoint.port() != 0);
}

void http1_upgrade() { websocket_round_trip(false, false); }

void standalone_accept_queue()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config server_config;
	server_config.http.protocols.requirement =
		http::protocol_requirement::require_http1;
	ws::server service(std::move(acceptor), server_config);
	service.bind({riwo::ip_type::v4, 0}).start();

	auto accepted = asio::co_spawn(context,
	[&]() -> riwo::awaitable<void>
	{
		auto connection = co_await service.accept(riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(connection.request.path, "/accept");
		auto message = co_await connection.stream.read<std::string>(
			riwo::use_awaitable);
		co_await connection.stream.write_text(message.body,
			riwo::use_awaitable);
	}, asio::use_future);

	ws::client_config client_config;
	client_config.http.protocols.requirement =
		http::protocol_requirement::require_http1;
	ws::client client(context, client_config);
	const auto endpoint = std::format("ws://127.0.0.1:{}/accept",
		service.http_server().acceptor().local_endpoint().port());
	auto connected = asio::co_spawn(context,
	[&]() -> riwo::awaitable<void>
	{
		auto stream = co_await client.open(endpoint, riwo::use_awaitable);
		co_await stream.write_text("queued accept", riwo::use_awaitable);
		auto response = co_await stream.read<std::string>(
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(response.body, "queued accept");
		riwo::error_code ignored;
		stream.cancel(ignored);
		service.stop();
		context.stop();
	}, asio::use_future);

	riwo::ignore_unused(context.run_for(std::chrono::seconds(10)));
	RIWO_TEST_CHECK(accepted.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	RIWO_TEST_CHECK(connected.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	accepted.get();
	connected.get();
}

void redirect_and_external_client()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement =
		http::protocol_requirement::require_http1;
	http::server service(std::move(acceptor), server_config);
	service.bind({riwo::ip_type::v4, 0});
	service.on_request<http::method::get>("/redirect",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		http::response_head response;
		response.status = http::status::temporary_redirect;
		response.headers.set("Location", "/echo");
		co_await exchange.respond(std::move(response), std::string_view{},
			riwo::use_awaitable);
	});
	service.on_request<http::method::get>("/echo",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		auto accepted = co_await ws::upgrade(exchange, riwo::use_awaitable);
		for(;;)
		{
			auto [error, message] = co_await accepted.stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			if( error ) break;
			co_await accepted.stream.write_text(message.body,
				riwo::use_awaitable);
		}
	});
	service.start();

	http::client_config client_config;
	client_config.protocols.requirement =
		http::protocol_requirement::require_http1;
	http::client http_client(context, client_config);
	const auto base = std::format("ws://127.0.0.1:{}",
		service.acceptor().local_endpoint().port());
	auto completed = asio::co_spawn(context,
	[&]() -> riwo::awaitable<void>
	{
		ws::open_diagnostics diagnostics;
		ws::connect_request limited(riwo::url(base + "/redirect"));
		auto [redirect_error, idle] = co_await ws::open(http_client,
			std::move(limited), diagnostics,
			asio::as_tuple(riwo::use_awaitable));
		RIWO_TEST_CHECK_EQ(redirect_error,
			ws::make_error_code(ws::errc::redirect_limit_exceeded));
		RIWO_TEST_CHECK(not idle.is_open());
		RIWO_TEST_CHECK(diagnostics.response.has_value());
		RIWO_TEST_CHECK_EQ(diagnostics.response->status,
			http::status::temporary_redirect);

		ws::connect_request followed(riwo::url(base + "/redirect"));
		followed.max_redirects = 1;
		auto stream = co_await ws::open(http_client, std::move(followed),
			diagnostics, riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(diagnostics.endpoint.path(), "/echo");
		co_await stream.write_text("redirected", riwo::use_awaitable);
		auto response = co_await stream.read<std::string>(
			riwo::use_awaitable);
		RIWO_TEST_CHECK_EQ(response.body, "redirected");
		riwo::error_code ignored;
		stream.cancel(ignored);
		service.stop();
		context.stop();
	}, asio::use_future);

	riwo::ignore_unused(context.run_for(std::chrono::seconds(10)));
	RIWO_TEST_CHECK(completed.wait_for(std::chrono::milliseconds(0)) ==
		std::future_status::ready);
	completed.get();
}

void cancel_pending_open()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement =
		http::protocol_requirement::require_http1;
	http::server service(std::move(acceptor), server_config);
	service.bind({riwo::ip_type::v4, 0});
	service.on_request<http::method::get>("/stall",
	[](http::server_exchange &exchange) -> riwo::awaitable<void>
	{
		asio::steady_timer timer(exchange.get_executor());
		timer.expires_after(std::chrono::seconds(10));
		auto [error] = co_await timer.async_wait(
			asio::as_tuple(riwo::use_awaitable));
		riwo::ignore_unused(error);
		exchange.cancel();
	});
	service.start();

	ws::client_config client_config;
	client_config.http.protocols.requirement =
		http::protocol_requirement::require_http1;
	ws::client client(context, client_config);
	const auto endpoint = std::format("ws://127.0.0.1:{}/stall",
		service.acceptor().local_endpoint().port());
	bool completed = false;
	client.open(endpoint, [&](riwo::error_code error, ws::stream stream)
	{
		RIWO_TEST_CHECK_EQ(error,
			asio::error::make_error_code(asio::error::operation_aborted));
		RIWO_TEST_CHECK(not stream.is_open());
		RIWO_TEST_CHECK_EQ(client.pending_open_count(), 0U);
		completed = true;
		service.stop();
		context.stop();
	});
	RIWO_TEST_CHECK_EQ(client.pending_open_count(), 1U);
	asio::steady_timer cancellation(context);
	cancellation.expires_after(std::chrono::milliseconds(10));
	cancellation.async_wait([&](riwo::error_code error)
	{
		if( not error ) client.cancel();
	});
	asio::steady_timer watchdog(context);
	watchdog.expires_after(std::chrono::seconds(2));
	watchdog.async_wait([&](riwo::error_code error)
	{
		if( not error )
		{
			service.stop();
			context.stop();
		}
	});
	context.run();
	RIWO_TEST_CHECK(completed);
}

#if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
void http1_upgrade_compression() { websocket_round_trip(false, true); }
#endif

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
void http2_extended_connect() { websocket_round_trip(true, false); }
# if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
void http2_extended_connect_compression() { websocket_round_trip(true, true); }
# endif
#endif

[[nodiscard]] std::string rejection_response(uint16_t port)
{
	riwo::io_context_t context;
	asio::ip::tcp::socket socket(context);
	riwo::error_code error;
	socket.connect({asio::ip::make_address("127.0.0.1"), port}, error);
	if( error ) throw std::system_error(error);

	static constexpr std::string_view request =
		"GET /deny HTTP/1.1\r\n"
		"Host: 127.0.0.1\r\n"
		"Connection: Upgrade\r\n"
		"Upgrade: websocket\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"\r\n";
	(void)asio::write(socket, asio::buffer(request), error);
	if( error ) throw std::system_error(error);

	asio::streambuf buffer;
	const auto head_size = asio::read_until(socket, buffer, "\r\n\r\n", error);
	if( error ) throw std::system_error(error);
	std::string response(asio::buffers_begin(buffer.data()),
		asio::buffers_begin(buffer.data()) +
			static_cast<std::ptrdiff_t>(head_size));
	buffer.consume(head_size);

	const auto marker = response.find("\r\nContent-Length:");
	if( marker == std::string::npos )
		return response;
	const auto value_begin = marker + std::string_view("\r\nContent-Length:").size();
	const auto value_end = response.find("\r\n", value_begin);
	const auto size = static_cast<std::size_t>(std::stoul(
		response.substr(value_begin, value_end - value_begin)));
	while( buffer.size() < size )
	{
		const auto read = socket.read_some(buffer.prepare(size - buffer.size()),
			error);
		if( error ) throw std::system_error(error);
		buffer.commit(read);
	}
	response.append(asio::buffers_begin(buffer.data()),
		asio::buffers_begin(buffer.data()) +
			static_cast<std::ptrdiff_t>(size));
	return response;
}

void upgrade_rejection_preserved()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config config;
	config.protocols.requirement = http::protocol_requirement::require_http1;
	http::server service(std::move(acceptor), config);
	service.bind({riwo::ip_type::v4, 0});
	service.on_request<http::method::get>("/deny",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			ws::upgrade_options options;
			options.request_validator = [](const ws::request_info &) {
				ws::upgrade_rejection rejection;
				rejection.status = http::status::forbidden;
				rejection.headers.add("X-Reject-Reason", "policy");
				rejection.body = "denied by policy";
				return ws::upgrade_validation_result {
					std::move(rejection)};
			};
			try {
				(void)co_await ws::upgrade(exchange, std::move(options),
					riwo::use_awaitable);
			}
			catch(const std::system_error &error) {
				RIWO_TEST_CHECK(error.code() ==
					make_error_code(ws::errc::handshake_rejected));
			}
		});
	service.start();

	const auto port = service.acceptor().local_endpoint().port();
	std::jthread server_thread([&] { context.run(); });
	const auto response = rejection_response(port);
	service.stop();
	context.stop();
	server_thread.join();

	RIWO_TEST_CHECK(response.starts_with("HTTP/1.1 403 Forbidden\r\n"));
	RIWO_TEST_CHECK(response.find("\r\nX-Reject-Reason: policy\r\n") !=
		std::string::npos);
	RIWO_TEST_CHECK(response.ends_with("denied by policy"));
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"HTTP/1 WebSocket upgrade", http1_upgrade},
		{"standalone server accept queue", standalone_accept_queue},
		{"redirect and external HTTP client", redirect_and_external_client},
		{"cancel pending open", cancel_pending_open},
#if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
		{"HTTP/1 WebSocket compression", http1_upgrade_compression},
#endif
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		{"HTTP/2 extended CONNECT", http2_extended_connect},
# if RIWO_WEBSOCKET_EXP_ZLIB_SUPPORT
		{"HTTP/2 extended CONNECT compression",
			http2_extended_connect_compression},
# endif
#endif
		{"validator rejection response", upgrade_rejection_preserved},
	});
}
