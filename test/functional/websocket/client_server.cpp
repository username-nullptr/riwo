// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/websocket/client.h>
#include <riwo/websocket/retry.h>
#include <riwo/websocket/server.h>
#include <riwo/websocket/detail/permessage_deflate.h>

namespace
{

namespace ws = riwo::websocket;

class observing_connector final : public riwo::http::connector
{
public:
	// Keep a non-owning handle so the adopted stream remains the sole owner.
	explicit observing_connector(executor_t exec) :
		riwo::http::connector(std::move(exec)) {}

	[[nodiscard]] connection_ptr last_connection() const noexcept
	{
		return m_last_connection.lock();
	}

protected:
	riwo::sys_expected<connection_ptr> do_connect(
		const riwo::http::connect_target &target
	) noexcept override
	{
		auto result = riwo::http::connector::do_connect(target);
		if( result )
			m_last_connection = *result;
		return result;
	}

	riwo::awaitable<riwo::sys_expected<connection_ptr>> co_do_connect(
		const riwo::http::connect_target &target
	) noexcept override
	{
		auto result = co_await riwo::http::connector::co_do_connect(target);
		if( result )
			m_last_connection = *result;
		co_return result;
	}

private:
	std::weak_ptr<connection_t> m_last_connection {};
};

void owned_handler_round_trip()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config server_config;
	server_config.default_upgrade.supported_subprotocols = {"owned.chat"};
	server_config.default_upgrade.require_subprotocol = true;
	server_config.default_upgrade.stream.ping_interval =
		std::chrono::milliseconds(19000);
	server_config.default_upgrade.stream.pong_timeout_retries = 4;
	ws::server service(std::move(acceptor), server_config);

	service.on_connection("/echo/{id}",
		[](ws::accept_result accepted) -> riwo::awaitable<void>
		{
			RIWO_TEST_CHECK_EQ(
				accepted.stream.config().ping_interval,
				std::chrono::milliseconds(19000));
			RIWO_TEST_CHECK_EQ(
				accepted.stream.config().pong_timeout_retries, 4U);
			auto id = accepted.request.path_arguments.find("id");
			const auto id_text = id == accepted.request.path_arguments.end() ?
				std::string("missing") : id->second.to_string();
			auto request = co_await accepted.stream.read<std::string>(
				riwo::use_awaitable);
			co_await accepted.stream.write_text(
				"owned:" + id_text + ": " + request.body,
				riwo::use_awaitable);
			auto close_result = co_await accepted.stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[close_error, trailing] = close_result;
			riwo::ignore_unused(close_error, trailing);
			co_return;
		});
	service.bind({riwo::ip_type::v4, 0}).start();

	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	ws::client client(context.get_executor());
	auto completed = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			ws::connect_request request(std::format(
				"ws://127.0.0.1:{}/echo/7", port));
			request.subprotocols = {"owned.chat"};
			auto stream = co_await client.open(
				std::move(request), riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(client.pending_open_count(), 0U);
			RIWO_TEST_CHECK_EQ(stream.negotiated_subprotocol(), "owned.chat");

			co_await stream.write_text("hello", riwo::use_awaitable);
			auto response = co_await stream.read<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(response.body, "owned:7: hello");
			auto close_result = co_await stream.close(
				asio::as_tuple(riwo::use_awaitable));
			auto &[close_error, closed] = close_result;
			service.stop();
			RIWO_TEST_CHECK(not close_error);
			RIWO_TEST_CHECK(closed.clean);
			co_return;
		}, asio::use_future);
	context.run();
	completed.get();
}

void owned_accept_round_trip()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	ws::upgrade_options options;
	options.supported_subprotocols = {"meta.v1", "meta.v2"};
	options.require_subprotocol = true;
	options.stream.ping_interval = std::chrono::milliseconds(17000);
	options.stream.pong_timeout_retries = 2;
	size_t selector_calls = 0;
	options.subprotocol_selector = [&](const ws::request_info &request,
		std::span<const std::string> offered)
		-> riwo::optional<std::string>
	{
		++selector_calls;
		if( request.path == "/accept/42" and
			request.request_headers.contains("X-WebSocket-Metadata") and
			offered.size() == 2 and offered[0] == "meta.v1" and
			offered[1] == "meta.v2" )
			return std::string("meta.v2");
		return riwo::nullopt;
	};

	auto accepted = asio::co_spawn(context,
		[&, options = std::move(options)]() mutable -> riwo::awaitable<void>
		{
			auto connection = co_await service.accept(
				std::move(options), riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(connection.stream.config().ping_interval,
				std::chrono::milliseconds(17000));
			RIWO_TEST_CHECK_EQ(
				connection.stream.config().pong_timeout_retries, 2U);
			RIWO_TEST_CHECK_EQ(connection.request.method,
				riwo::http::method::get);
			RIWO_TEST_CHECK_EQ(connection.request.version,
				riwo::http::version::v11);
			RIWO_TEST_CHECK_EQ(connection.request.target,
				"/accept/42?mode=full");
			RIWO_TEST_CHECK_EQ(connection.request.path, "/accept/42");
			RIWO_TEST_CHECK_EQ(connection.request.request_headers
				.at("X-WebSocket-Metadata").to_string(), "present");
			auto mode = connection.request.query_parameters.find("mode");
			RIWO_TEST_CHECK(mode !=
				connection.request.query_parameters.end());
			RIWO_TEST_CHECK_EQ(mode->second.to_string(), "full");
			RIWO_TEST_CHECK(connection.request.path_arguments.empty());
			RIWO_TEST_CHECK(connection.request.remote_endpoint.port != 0);
			RIWO_TEST_CHECK_EQ(connection.request.local_endpoint.port, port);
			RIWO_TEST_CHECK_EQ(connection.handshake.subprotocol, "meta.v2");
			RIWO_TEST_CHECK(connection.handshake.extensions.empty());
			RIWO_TEST_CHECK_EQ(connection.stream.negotiated_subprotocol(),
				"meta.v2");
			RIWO_TEST_CHECK(connection.stream.negotiated_extensions().empty());
			auto message = co_await connection.stream.read<std::string>(
				riwo::use_awaitable);
			co_await connection.stream.write_text(
				message.body, riwo::use_awaitable);
			auto close_result = co_await connection.stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[close_error, trailing] = close_result;
			riwo::ignore_unused(close_error, trailing);
			co_return;
		}, asio::use_future);

	ws::client_config client_config;
	client_config.stream.ping_interval =
		std::chrono::milliseconds(23000);
	client_config.stream.pong_timeout_retries = 3;
	ws::client client(context.get_executor(), client_config);
	auto connected = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			ws::connect_request request(std::format(
				"ws://127.0.0.1:{}/accept/42?mode=full", port));
			request.subprotocols = {"meta.v1", "meta.v2"};
			request.request_options.set_header(
				"X-WebSocket-Metadata", "present");
			auto stream = co_await client.open(
				std::move(request), riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(stream.config().ping_interval,
				std::chrono::milliseconds(23000));
			RIWO_TEST_CHECK_EQ(stream.config().pong_timeout_retries, 3U);
			RIWO_TEST_CHECK_EQ(stream.negotiated_subprotocol(), "meta.v2");
			co_await stream.write_text("accept-mode", riwo::use_awaitable);
			auto response = co_await stream.read<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(response.body, "accept-mode");
			auto close_result = co_await stream.close(
				asio::as_tuple(riwo::use_awaitable));
			auto &[close_error, closed] = close_result;
			service.stop();
			RIWO_TEST_CHECK(not close_error);
			RIWO_TEST_CHECK(closed.clean);
			co_return;
		}, asio::use_future);

	context.run();
	accepted.get();
	connected.get();
	RIWO_TEST_CHECK_EQ(selector_calls, 1U);
}

void owned_configuration_and_resources()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	riwo::http::client http_client(context.get_executor());
	auto cookie_store = http_client.cookie_store();

	ws::client_config client_config;
	client_config.handshake_timeout = 321ms;
	client_config.stream.max_message_size = 4096;
	client_config.stream.ping_interval = 7s;
	client_config.stream.pong_timeout_retries = 5;
	client_config.no_delay = riwo::nullopt;
	ws::client original(std::move(http_client), client_config);
	RIWO_TEST_CHECK_EQ(original.config().handshake_timeout, 321ms);
	RIWO_TEST_CHECK_EQ(original.config().stream.max_message_size, 4096U);
	RIWO_TEST_CHECK_EQ(original.config().stream.ping_interval, 7s);
	RIWO_TEST_CHECK_EQ(original.config().stream.pong_timeout_retries, 5U);
	RIWO_TEST_CHECK(not original.config().no_delay.has_value());
	RIWO_TEST_CHECK_EQ(original.cookie_store(), cookie_store);
	RIWO_TEST_CHECK(original.get_executor() == context.get_executor());
	const auto &const_client = original;
	RIWO_TEST_CHECK_EQ(&const_client.http_client(), &original.http_client());

	ws::client moved(std::move(original));
	ws::client assigned(context.get_executor());
	assigned = std::move(moved);
	RIWO_TEST_CHECK_EQ(assigned.cookie_store(), cookie_store);
	RIWO_TEST_CHECK_EQ(assigned.config().handshake_timeout, 321ms);

	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config server_config;
	server_config.max_pending_handshakes = 7;
	server_config.default_upgrade.stream.ping_interval = 9s;
	server_config.default_upgrade.stream.pong_timeout_retries = 6;
	ws::server service(std::move(acceptor), server_config);
	RIWO_TEST_CHECK_EQ(service.config().max_pending_handshakes, 7U);
	RIWO_TEST_CHECK_EQ(service.config().default_upgrade.stream.ping_interval,
		9s);
	RIWO_TEST_CHECK_EQ(
		service.config().default_upgrade.stream.pong_timeout_retries, 6U);
	RIWO_TEST_CHECK(service.get_executor() == context.get_executor());
	const auto &const_service = service;
	RIWO_TEST_CHECK_EQ(&const_service.http_server(), &service.http_server());

	server_config.max_pending_handshakes = 3;
	server_config.pending_handshake_timeout = 456ms;
	service.set_config(server_config);
	RIWO_TEST_CHECK_EQ(service.config().max_pending_handshakes, 3U);
	RIWO_TEST_CHECK_EQ(service.config().pending_handshake_timeout, 456ms);
}

void client_no_delay_modes()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	service.on_default([](ws::accept_result accepted) -> riwo::awaitable<void>
	{
		auto read_result = co_await accepted.stream.read<std::string>(
			asio::as_tuple(riwo::use_awaitable));
		auto &[error, message] = read_result;
		riwo::ignore_unused(error, message);
	});
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	const auto endpoint = std::format("ws://127.0.0.1:{}/no-delay", port);

	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		auto verify = [&](bool http_no_delay, riwo::optional<bool> ws_no_delay,
			bool expected) -> riwo::awaitable<void>
		{
			auto connector = std::make_shared<observing_connector>(
				context.get_executor());
			riwo::http::connection_pool pool(connector);
			riwo::http::client_config http_config;
			http_config.no_delay = http_no_delay;
			riwo::http::client http_client(std::move(pool), http_config);

			ws::client_config ws_config;
			ws_config.no_delay = ws_no_delay;
			ws::client client(std::move(http_client), ws_config);
			auto stream = co_await client.open(endpoint, riwo::use_awaitable);

			auto connection = connector->last_connection();
			RIWO_TEST_CHECK(connection);
			auto options = connection->options();
			RIWO_TEST_CHECK(options);
			RIWO_TEST_CHECK_EQ(options->no_delay, expected);
			riwo::ignore_unused(co_await stream.close(riwo::use_awaitable));
		};

		try {
			co_await verify(false, true, true);
			co_await verify(true, false, false);
			co_await verify(false, riwo::nullopt, false);
		}
		catch(...)
		{
			service.stop();
			throw;
		}
		service.stop();
	}, asio::use_future);

	context.run();
	completed.get();
}

void delivery_modes_and_cancellation()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));

	bool completed = false;
	service.accept([&](riwo::error_code error, ws::accept_result result)
	{
		RIWO_TEST_CHECK_EQ(error,
			riwo::error_code(asio::error::operation_aborted));
		RIWO_TEST_CHECK(not result.stream.is_open());
		completed = true;
	});
	RIWO_TEST_CHECK_EQ(service.pending_accept_count(), 1U);
	RIWO_TEST_CHECK_THROWS(
		service.on_default([](ws::accept_result) -> riwo::awaitable<void> {
			co_return;
		}), std::logic_error);
	service.cancel();
	context.run();
	RIWO_TEST_CHECK(completed);
	RIWO_TEST_CHECK_EQ(service.pending_accept_count(), 0U);

	riwo::io_context_t other_context;
	asio::ip::tcp::acceptor other_acceptor(other_context);
	ws::server callback_service(std::move(other_acceptor));
	callback_service.on_default(
		[](ws::accept_result) -> riwo::awaitable<void> { co_return; });
	bool rejected = false;
	callback_service.accept(
		[&](riwo::error_code error, ws::accept_result result)
		{
			RIWO_TEST_CHECK_EQ(error,
				std::make_error_code(std::errc::operation_not_supported));
			RIWO_TEST_CHECK(not result.stream.is_open());
			rejected = true;
		});
	other_context.run();
	RIWO_TEST_CHECK(rejected);
	riwo::error_code sync_error;
	auto sync_result = callback_service.accept(sync_error);
	RIWO_TEST_CHECK_EQ(sync_error,
		std::make_error_code(std::errc::operation_not_supported));
	RIWO_TEST_CHECK(not sync_result.stream.is_open());
}

void pending_handshake_queue()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config config;
	config.pending_handshake_timeout = 30s;
	ws::server service(std::move(acceptor), config);

	// Fix the server in accept mode, then remove the initial waiter so that the
	// incoming request has to wait in the handshake queue.
	asio::cancellation_signal cancellation;
	bool initial_cancelled = false;
	service.accept(asio::bind_cancellation_slot(cancellation.slot(),
		[&](riwo::error_code error, ws::accept_result)
		{
			RIWO_TEST_CHECK_EQ(error,
				riwo::error_code(asio::error::operation_aborted));
			initial_cancelled = true;
		}));
	cancellation.emit(asio::cancellation_type::all);
	context.poll();
	RIWO_TEST_CHECK(initial_cancelled);
	context.restart();

	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	ws::client client(context.get_executor());
	bool watchdog_expired = false;
	asio::steady_timer watchdog(context.get_executor());
	watchdog.expires_after(5s);
	watchdog.async_wait([&](riwo::error_code error)
	{
		if( error == asio::error::operation_aborted )
			return;
		watchdog_expired = true;
		client.cancel();
		service.stop();
	});
	auto connected = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			auto stream = co_await client.open(std::format(
				"ws://127.0.0.1:{}/queued", port), riwo::use_awaitable);
			co_await stream.write_text("queued", riwo::use_awaitable);
			auto response = co_await stream.read<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(response.body, "queued");
			riwo::ignore_unused(co_await stream.close(riwo::use_awaitable));
			riwo::ignore_unused(watchdog.cancel());
			service.stop();
			co_return;
		}, asio::use_future);
	auto accepted = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			asio::steady_timer retry(context.get_executor());
			while( service.pending_handshake_count() == 0 and
				not watchdog_expired )
			{
				retry.expires_after(1ms);
				co_await retry.async_wait(riwo::use_awaitable);
			}
			if( watchdog_expired )
				co_return;
			RIWO_TEST_CHECK_EQ(service.pending_handshake_count(), 1U);
			auto connection = co_await service.accept(riwo::use_awaitable);
			auto message = co_await connection.stream.read<std::string>(
				riwo::use_awaitable);
			co_await connection.stream.write_text(
				message.body, riwo::use_awaitable);
			riwo::ignore_unused(co_await connection.stream.close(
				riwo::use_awaitable));
			co_return;
		}, asio::use_future);

	context.run();
	RIWO_TEST_CHECK(not watchdog_expired);
	connected.get();
	accepted.get();
}

void unavailable_handshake_queue()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	auto config = service.config();
	config.max_pending_handshakes = 0;
	config.default_upgrade.handshake_timeout = 1s;
	service.set_config(config);

	// Enter accept mode and remove the only waiter. With queueing disabled, the
	// next otherwise-valid opening request must receive a bounded 503 response.
	asio::cancellation_signal cancellation;
	bool initial_cancelled = false;
	service.accept(asio::bind_cancellation_slot(cancellation.slot(),
		[&](riwo::error_code error, ws::accept_result)
		{
			RIWO_TEST_CHECK_EQ(error,
				riwo::error_code(asio::error::operation_aborted));
			initial_cancelled = true;
		}));
	cancellation.emit(asio::cancellation_type::all);
	context.poll();
	RIWO_TEST_CHECK(initial_cancelled);
	context.restart();

	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	ws::client client(context.get_executor());
	auto completed = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			ws::open_diagnostics diagnostics;
			auto open_result = co_await client.open(ws::connect_request(
				std::format("ws://127.0.0.1:{}/unavailable", port)),
				diagnostics, asio::as_tuple(riwo::use_awaitable));
			auto &[error, stream] = open_result;
			RIWO_TEST_CHECK_EQ(error,
				ws::make_error_code(ws::errc::handshake_rejected));
			RIWO_TEST_CHECK(not stream.is_open());
			RIWO_TEST_CHECK(diagnostics.reply);
			RIWO_TEST_CHECK_EQ(diagnostics.reply->status(),
				riwo::http::status::service_unavailable);
			RIWO_TEST_CHECK_EQ(co_await diagnostics.reply->read<std::string>(
				riwo::use_awaitable), "WebSocket accept queue unavailable\n");
			RIWO_TEST_CHECK_EQ(service.pending_handshake_count(), 0U);
			service.stop();
			co_return;
		}, asio::use_future);
	context.run();
	completed.get();
}

void owned_client_cancellation()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context,
		{asio::ip::address_v4::loopback(), 0});
	asio::ip::tcp::socket peer(context);
	acceptor.async_accept(peer, [](riwo::error_code error) {
		RIWO_TEST_CHECK(not error);
	});

	ws::client_config config;
	config.handshake_timeout = 1s;
	ws::client client(context.get_executor(), config);
	const auto port = acceptor.local_endpoint().port();
	auto opening = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			auto open_result = co_await client.open(
				std::format("ws://127.0.0.1:{}/stall", port),
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, stream] = open_result;
			RIWO_TEST_CHECK_EQ(error,
				riwo::error_code(asio::error::operation_aborted));
			RIWO_TEST_CHECK(not stream.is_open());
			RIWO_TEST_CHECK_EQ(client.pending_open_count(), 0U);
			co_return;
		}, asio::use_future);
	auto cancellation = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			asio::steady_timer delay(context.get_executor());
			delay.expires_after(10ms);
			co_await delay.async_wait(riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(client.pending_open_count(), 1U);
			client.cancel();
			co_return;
		}, asio::use_future);

	context.run();
	opening.get();
	cancellation.get();
}

void owned_client_handshake_timeout()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context,
		{asio::ip::address_v4::loopback(), 0});
	asio::ip::tcp::socket peer(context);
	acceptor.async_accept(peer, [](riwo::error_code error) {
		RIWO_TEST_CHECK(not error);
	});

	ws::client_config config;
	config.handshake_timeout = 20ms;
	ws::client client(context.get_executor(), config);
	const auto port = acceptor.local_endpoint().port();
	auto opening = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			auto open_result = co_await client.open(
				std::format("ws://127.0.0.1:{}/stall", port),
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, stream] = open_result;
			RIWO_TEST_CHECK_EQ(error,
				riwo::error_code(asio::error::timed_out));
			RIWO_TEST_CHECK(not stream.is_open());
			RIWO_TEST_CHECK_EQ(client.pending_open_count(), 0U);
		}, asio::use_future);
	context.run();
	opening.get();
}

void pending_handshake_timeout()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config config;
	config.max_pending_handshakes = 1;
	config.pending_handshake_timeout = 20ms;
	config.default_upgrade.handshake_timeout = 1s;
	ws::server service(std::move(acceptor), config);

	asio::cancellation_signal cancellation;
	service.accept(asio::bind_cancellation_slot(cancellation.slot(),
		[](riwo::error_code error, ws::accept_result)
		{
			RIWO_TEST_CHECK_EQ(error,
				riwo::error_code(asio::error::operation_aborted));
		}));
	cancellation.emit(asio::cancellation_type::all);
	context.poll();
	context.restart();

	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	ws::client client(context.get_executor());
	auto completed = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			ws::open_diagnostics diagnostics;
			auto open_result = co_await client.open(ws::connect_request(
				std::format("ws://127.0.0.1:{}/timeout", port)), diagnostics,
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, stream] = open_result;
			RIWO_TEST_CHECK_EQ(error,
				ws::make_error_code(ws::errc::handshake_rejected));
			RIWO_TEST_CHECK(not stream.is_open());
			RIWO_TEST_CHECK(diagnostics.reply);
			RIWO_TEST_CHECK_EQ(diagnostics.reply->status(),
				riwo::http::status::service_unavailable);
			RIWO_TEST_CHECK_EQ(service.pending_handshake_count(), 0U);
			service.stop();
		}, asio::use_future);
	context.run();
	completed.get();
}

void pending_handshake_fifo_and_capacity()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config config;
	config.max_pending_handshakes = 2;
	config.pending_handshake_timeout = 1s;
	ws::server service(std::move(acceptor), config);

	asio::cancellation_signal cancellation;
	service.accept(asio::bind_cancellation_slot(cancellation.slot(),
		[](riwo::error_code error, ws::accept_result)
		{
			RIWO_TEST_CHECK_EQ(error,
				riwo::error_code(asio::error::operation_aborted));
		}));
	cancellation.emit(asio::cancellation_type::all);
	context.poll();
	context.restart();

	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	const auto endpoint = [&](std::string_view path) {
		return std::format("ws://127.0.0.1:{}{}", port, path);
	};
	ws::client first_client(context.get_executor());
	ws::client second_client(context.get_executor());
	ws::client overflow_client(context.get_executor());
	bool overflow_rejected = false;

	auto first = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			auto stream = co_await first_client.open(
				endpoint("/first"), riwo::use_awaitable);
			auto closed = co_await stream.close(riwo::use_awaitable);
			RIWO_TEST_CHECK(closed.clean);
		}, asio::use_future);

	auto second = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			asio::steady_timer delay(context.get_executor());
			while( service.pending_handshake_count() < 1 )
			{
				delay.expires_after(1ms);
				co_await delay.async_wait(riwo::use_awaitable);
			}
			auto stream = co_await second_client.open(
				endpoint("/second"), riwo::use_awaitable);
			auto closed = co_await stream.close(riwo::use_awaitable);
			RIWO_TEST_CHECK(closed.clean);
		}, asio::use_future);

	auto overflow = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			asio::steady_timer delay(context.get_executor());
			while( service.pending_handshake_count() < 2 )
			{
				delay.expires_after(1ms);
				co_await delay.async_wait(riwo::use_awaitable);
			}
			ws::open_diagnostics diagnostics;
			auto open_result = co_await overflow_client.open(
				ws::connect_request(endpoint("/overflow")), diagnostics,
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, stream] = open_result;
			RIWO_TEST_CHECK_EQ(error,
				ws::make_error_code(ws::errc::handshake_rejected));
			RIWO_TEST_CHECK(not stream.is_open());
			RIWO_TEST_CHECK(diagnostics.reply);
			RIWO_TEST_CHECK_EQ(diagnostics.reply->status(),
				riwo::http::status::service_unavailable);
			overflow_rejected = true;
		}, asio::use_future);

	auto accepted = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			asio::steady_timer delay(context.get_executor());
			while( not overflow_rejected )
			{
				delay.expires_after(1ms);
				co_await delay.async_wait(riwo::use_awaitable);
			}
			RIWO_TEST_CHECK_EQ(service.pending_handshake_count(), 2U);
			auto first_connection = co_await service.accept(riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(first_connection.request.path, "/first");
			RIWO_TEST_CHECK((co_await first_connection.stream.close(
				riwo::use_awaitable)).clean);

			auto second_connection = co_await service.accept(riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(second_connection.request.path, "/second");
			RIWO_TEST_CHECK((co_await second_connection.stream.close(
				riwo::use_awaitable)).clean);
			service.stop();
		}, asio::use_future);

	context.run();
	first.get();
	second.get();
	overflow.get();
	accepted.get();
}

void simultaneous_close()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();

	auto accepted = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			auto connection = co_await service.accept(riwo::use_awaitable);
			auto closed = co_await connection.stream.close(
				ws::close_frame(ws::close_code::normal_closure, "server"),
				riwo::use_awaitable);
			RIWO_TEST_CHECK(closed.clean);
			RIWO_TEST_CHECK_EQ(closed.code.value_or(0), 1000);
		}, asio::use_future);

	ws::client client(context.get_executor());
	auto connected = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			auto stream = co_await client.open(std::format(
				"ws://127.0.0.1:{}/close", port), riwo::use_awaitable);
			auto closed = co_await stream.close(
				ws::close_frame(ws::close_code::normal_closure, "client"),
				riwo::use_awaitable);
			RIWO_TEST_CHECK(closed.clean);
			RIWO_TEST_CHECK_EQ(closed.code.value_or(0), 1000);
			service.stop();
		}, asio::use_future);

	context.run();
	accepted.get();
	connected.get();
}

void explicit_retry_open_recovery()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	size_t accepted_count = 0;
	service.on_default([&](ws::accept_result accepted) -> riwo::awaitable<void>
	{
		const auto index = ++accepted_count;
		co_await accepted.stream.write_text(std::to_string(index),
			riwo::use_awaitable);
		if( index == 1 )
		{
			auto close_result = co_await accepted.stream.close(
				ws::close_frame(ws::close_code::service_restart, "restart"),
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, close] = close_result;
			RIWO_TEST_CHECK(not error);
			RIWO_TEST_CHECK(close.clean);
		}
		else
		{
			auto read_result = co_await accepted.stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, trailing] = read_result;
			RIWO_TEST_CHECK(error);
			riwo::ignore_unused(trailing);
		}
	});
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	const auto endpoint = std::format("ws://127.0.0.1:{}/recovery", port);

	asio::ip::tcp::acceptor unavailable(context,
		{asio::ip::address_v4::loopback(), 0});
	const auto unavailable_port = unavailable.local_endpoint().port();
	unavailable.close();

	std::vector<ws::retry_open_event> observed_events;
	ws::retry_open_options options;
	options.initial_delay = 1ms;
	options.max_delay = 2ms;
	options.jitter = 0.0;
	options.max_attempts = 4;
	options.observe = [&](ws::retry_open_event event,
		const ws::retry_open_context&) {
		observed_events.push_back(event);
	};
	ws::client client(context.get_executor());

	size_t request_count = 0;
	std::vector<std::string> messages;
	auto completed = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			// Startup remains one ordinary open outside retry_open().
			auto stream = co_await client.open(
				ws::connect_request(endpoint), riwo::use_awaitable);
			auto message = co_await stream.read<std::string>(
				riwo::use_awaitable);
			messages.push_back(std::move(message.body));

			auto read_result = co_await stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[read_error, trailing] = read_result;
			RIWO_TEST_CHECK(read_error);
			riwo::ignore_unused(trailing);
			stream.shutdown();

			auto recovered = co_await ws::retry_open(client,
				[&](const ws::retry_open_context &previous)
					-> ws::connect_request
				{
					++request_count;
					RIWO_TEST_CHECK_EQ(previous.attempt,
						request_count - 1);
					const auto target = request_count == 1 ?
						std::format("ws://127.0.0.1:{}/unavailable",
							unavailable_port) : endpoint;
					ws::connect_request request(target);
					request.request_options.set_header("X-Recovery-Attempt",
						std::to_string(request_count));
					return request;
				}, options, riwo::use_awaitable);

			RIWO_TEST_CHECK_EQ(recovered.attempts, 2U);
			RIWO_TEST_CHECK(recovered.last_failure.error);
			RIWO_TEST_CHECK_EQ(recovered.last_failure.attempt, 1U);
			RIWO_TEST_CHECK_EQ(recovered.diagnostics.endpoint.to_string(),
				endpoint);

			stream = std::move(recovered.stream);
			message = co_await stream.read<std::string>(riwo::use_awaitable);
			messages.push_back(std::move(message.body));
			auto closed = co_await stream.close(riwo::use_awaitable);
			RIWO_TEST_CHECK(closed.clean);
			service.stop();
		}, asio::use_future);

	context.run();
	completed.get();
	RIWO_TEST_CHECK_EQ(request_count, 2U);
	RIWO_TEST_CHECK_EQ(accepted_count, 2U);
	RIWO_TEST_CHECK_EQ(messages,
		(std::vector<std::string>{"1", "2"}));
	RIWO_TEST_CHECK_EQ(std::ranges::count(observed_events,
		ws::retry_open_event::opening), 2);
	RIWO_TEST_CHECK_EQ(std::ranges::count(observed_events,
		ws::retry_open_event::waiting), 1);
}

void retry_open_cancellation()
{
	using namespace std::chrono_literals;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor probe(context,
		{asio::ip::address_v4::loopback(), 0});
	const auto port = probe.local_endpoint().port();
	probe.close();

	ws::retry_open_options options;
	options.initial_delay = 1s;
	options.max_delay = 1s;
	options.jitter = 0.0;
	options.decide = [](const ws::retry_open_context&,
		std::chrono::milliseconds suggested) {
		return ws::retry_open_decision::retry_after(suggested);
	};
	bool waiting = false;
	asio::cancellation_signal cancellation;
	options.observe = [&](ws::retry_open_event event,
		const ws::retry_open_context&) {
		if( event != ws::retry_open_event::waiting )
			return;
		waiting = true;
		asio::post(context, [&] {
			cancellation.emit(asio::cancellation_type::all);
		});
	};
	ws::client client(context.get_executor());

	bool completed = false;
	riwo::error_code completion_error;
	ws::retry_open(client, ws::connect_request(std::format(
		"ws://127.0.0.1:{}/cancel", port)), options,
		asio::bind_cancellation_slot(cancellation.slot(),
		[&](riwo::error_code error, ws::retry_open_result result)
		{
			completion_error = error;
			RIWO_TEST_CHECK_EQ(result.attempts, 1U);
			RIWO_TEST_CHECK_EQ(result.last_failure.error,
				asio::error::make_error_code(asio::error::operation_aborted));
			completed = true;
		}));

	context.run();
	RIWO_TEST_CHECK(waiting);
	RIWO_TEST_CHECK(completed);
	RIWO_TEST_CHECK_EQ(completion_error,
		asio::error::make_error_code(asio::error::operation_aborted));
	RIWO_TEST_CHECK_EQ(client.pending_open_count(), 0U);
}

#if RIWO_WEBSOCKET_ZLIB_SUPPORT
void permessage_deflate_round_trip()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor));
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();

	ws::upgrade_options options;
	ws::permessage_deflate_options server_compression;
	server_compression.server_max_window_bits = 11;
	server_compression.client_max_window_bits = 10;
	auto server_extension_policy =
		ws::permessage_deflate_extension(server_compression);
	options.supported_extensions = {
		server_extension_policy
	};
	size_t extension_selector_calls = 0;
	options.extension_selector =
		[&, server_extension_policy](const ws::request_info &request,
			std::span<const ws::extension> offered)
			-> std::vector<ws::extension>
	{
		++extension_selector_calls;
		if( request.path != "/compressed" or offered.size() != 1 )
			return {};
		auto selected = ws::detail::negotiate_permessage_deflate(
			offered.front(), server_extension_policy);
		return selected ? std::vector<ws::extension>{std::move(*selected)} :
			std::vector<ws::extension>{};
	};
	options.stream.write_fragment_size = 7;
	auto accepted = asio::co_spawn(context,
		[&, options = std::move(options)]() mutable -> riwo::awaitable<void>
		{
			auto connection = co_await service.accept(
				std::move(options), riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(connection.handshake.extensions.size(), 1U);
			RIWO_TEST_CHECK(ws::is_permessage_deflate_extension(
				connection.handshake.extensions.front()));
			RIWO_TEST_CHECK_EQ(connection.stream.negotiated_extensions().size(), 1U);

			for(size_t index = 0; index < 4; ++index)
			{
				auto request = co_await connection.stream.read<>(
					riwo::use_awaitable);
				co_await connection.stream.write(request.type,
					riwo::const_buffer(request.body.data(), request.body.size()),
					riwo::use_awaitable);
			}
			auto close_result = co_await connection.stream.read<>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[close_error, trailing] = close_result;
			riwo::ignore_unused(close_error, trailing);
		}, asio::use_future);

	ws::client client(context.get_executor());
	auto connected = asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			ws::connect_request request(std::format(
				"ws://127.0.0.1:{}/compressed", port));
			ws::permessage_deflate_options client_compression;
			client_compression.server_max_window_bits = 12;
			client_compression.offer_client_max_window_bits = true;
			request.extensions = {
				ws::permessage_deflate_extension(client_compression)
			};
			ws::stream_config stream_config;
			stream_config.write_fragment_size = 5;
			stream_config.compression.min_message_size = 16;
			stream_config.compression.level = 6;
			request.stream_options = stream_config;
			auto stream = co_await client.open(
				std::move(request), riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(stream.negotiated_extensions().size(), 1U);

			RIWO_TEST_CHECK_EQ(co_await stream.write_text(
				"", riwo::use_awaitable), 0U);
			auto empty = co_await stream.read_frame<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(empty.type, ws::message_type::text);
			RIWO_TEST_CHECK(empty.body.empty());
			RIWO_TEST_CHECK(empty.fin);

			const std::array<std::byte,8> binary_payload {
				std::byte {0x00}, std::byte {0xFF}, std::byte {0x01},
				std::byte {0x02}, std::byte {0x80}, std::byte {0x7F},
				std::byte {0x00}, std::byte {0x55}
			};
			RIWO_TEST_CHECK_EQ(co_await stream.write_binary(
				riwo::const_buffer(binary_payload.data(), binary_payload.size()),
				ws::write_options {.compression = ws::compression_mode::enabled},
				riwo::use_awaitable), binary_payload.size());
			std::vector<std::byte> binary;
			auto binary_info = co_await stream.consume(
				[&](const ws::message_chunk &chunk) {
					const auto *data = static_cast<const std::byte*>(chunk.body.data());
					binary.insert(binary.end(), data, data + chunk.body.size());
				}, riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(binary_info.type, ws::message_type::binary);
			RIWO_TEST_CHECK(std::ranges::equal(binary, binary_payload));

			ws::basic_data_frame<std::string> frame {
				.type = ws::message_type::text,
				.body = "frame",
			};
			RIWO_TEST_CHECK_EQ(co_await stream.write_frame(
				frame, riwo::use_awaitable), frame.body.size());
			auto frame_echo = co_await stream.read<std::string>(riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(frame_echo.body, frame.body);

			std::string payload;
			for(size_t index = 0; index < 128; ++index)
				payload += "compressible websocket payload ";
			RIWO_TEST_CHECK_EQ(co_await stream.write_text(
				payload,
				ws::write_options {.compression = ws::compression_mode::disabled},
				riwo::use_awaitable), payload.size());
			auto response = co_await stream.read<std::string>(
				riwo::use_awaitable);
			RIWO_TEST_CHECK_EQ(response.body, payload);
			auto closed = co_await stream.close(riwo::use_awaitable);
			RIWO_TEST_CHECK(closed.clean);
			service.stop();
		}, asio::use_future);

	context.run();
	accepted.get();
	connected.get();
	RIWO_TEST_CHECK_EQ(extension_selector_calls, 1U);
}
#endif

void invalid_owned_config()
{
	riwo::io_context_t context;
	ws::client preflight_client(context.get_executor());
	riwo::error_code error;
	auto stream = preflight_client.open(
		ws::connect_request("ftp://example.test/socket"), error);
	RIWO_TEST_CHECK_EQ(error,
		std::make_error_code(std::errc::protocol_not_supported));
	RIWO_TEST_CHECK(not stream.is_open());
	RIWO_TEST_CHECK_EQ(preflight_client.pending_open_count(), 0U);

	ws::connect_request unsupported("ws://example.test/socket");
	unsupported.extensions.push_back({
		.name = "permessage-deflate",
		.parameters = {{.name = "unknown"}},
	});
	stream = preflight_client.open(std::move(unsupported), error);
	RIWO_TEST_CHECK_EQ(error,
		ws::make_error_code(ws::errc::unsupported_extension));
	RIWO_TEST_CHECK(not stream.is_open());
	RIWO_TEST_CHECK_EQ(preflight_client.pending_open_count(), 0U);

#if !RIWO_WEBSOCKET_ZLIB_SUPPORT
	ws::connect_request unavailable("ws://example.test/socket");
	unavailable.extensions = {ws::permessage_deflate_extension()};
	stream = preflight_client.open(std::move(unavailable), error);
	RIWO_TEST_CHECK_EQ(error,
		ws::make_error_code(ws::errc::unsupported_extension));
	RIWO_TEST_CHECK(not stream.is_open());
#endif

	ws::client_config client_config;
	client_config.stream.read_buffer_size = 0;
	RIWO_TEST_CHECK_THROWS(ws::client(client_config), std::system_error);
	client_config.stream.read_buffer_size = ws::stream_config{}.read_buffer_size;
	client_config.stream.ping_interval = std::chrono::milliseconds(-1);
	RIWO_TEST_CHECK_THROWS(ws::client(client_config), std::system_error);
	client_config.stream.ping_interval =
		ws::stream_config{}.ping_interval;

	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config server_config;
	server_config.default_upgrade.stream.read_buffer_size = 0;
	RIWO_TEST_CHECK_THROWS(
		ws::server(std::move(acceptor), server_config), std::system_error);

	asio::ip::tcp::acceptor ping_acceptor(context);
	server_config.default_upgrade.stream.read_buffer_size =
		ws::stream_config{}.read_buffer_size;
	server_config.default_upgrade.stream.ping_interval =
		std::chrono::milliseconds(-1);
	RIWO_TEST_CHECK_THROWS(
		ws::server(std::move(ping_acceptor), server_config), std::system_error);

}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"owned handler round trip", owned_handler_round_trip},
		{"owned accept round trip", owned_accept_round_trip},
		{"owned configuration and resources", owned_configuration_and_resources},
		{"client TCP_NODELAY modes", client_no_delay_modes},
		{"delivery modes and cancellation", delivery_modes_and_cancellation},
		{"pending handshake queue", pending_handshake_queue},
		{"unavailable handshake queue", unavailable_handshake_queue},
		{"owned client cancellation", owned_client_cancellation},
		{"owned client handshake timeout", owned_client_handshake_timeout},
		{"pending handshake timeout", pending_handshake_timeout},
		{"pending handshake FIFO and capacity",
			pending_handshake_fifo_and_capacity},
		{"simultaneous close", simultaneous_close},
		{"explicit retry open recovery", explicit_retry_open_recovery},
		{"retry open cancellation", retry_open_cancellation},
#if RIWO_WEBSOCKET_ZLIB_SUPPORT
		{"permessage-deflate round trip", permessage_deflate_round_trip},
#endif
		{"invalid owned config", invalid_owned_config},
	});
}
