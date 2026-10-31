// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"
#include <riwo/experimental/websocket.h>

#include <ranges>
#include <string>
#include <vector>

namespace
{

namespace http = riwo::experimental::http;
namespace ws = riwo::experimental::websocket;

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
	const auto port = service.http_server().acceptor().local_endpoint().port();
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
	const auto port = service.http_server().acceptor().local_endpoint().port();
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
			RIWO_TEST_CHECK(diagnostics.response);
			RIWO_TEST_CHECK_EQ(diagnostics.response->status,
				http::status::service_unavailable);
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
	const auto port = service.http_server().acceptor().local_endpoint().port();
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
			RIWO_TEST_CHECK(diagnostics.response);
			RIWO_TEST_CHECK_EQ(diagnostics.response->status,
				http::status::service_unavailable);
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
	const auto port = service.http_server().acceptor().local_endpoint().port();
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
			RIWO_TEST_CHECK(diagnostics.response);
			RIWO_TEST_CHECK_EQ(diagnostics.response->status,
				http::status::service_unavailable);
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
	const auto port = service.http_server().acceptor().local_endpoint().port();

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
	const auto port = service.http_server().acceptor().local_endpoint().port();
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
					request.headers.set("X-Recovery-Attempt",
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

#if !RIWO_EXP_WEBSOCKET_ZLIB_SUPPORT
	ws::connect_request unavailable("ws://example.test/socket");
	unavailable.extensions = {ws::permessage_deflate_extension()};
	stream = preflight_client.open(std::move(unavailable), error);
	RIWO_TEST_CHECK_EQ(error,
		ws::make_error_code(ws::errc::unsupported_extension));
	RIWO_TEST_CHECK(not stream.is_open());
#endif

	ws::client_config client_config;
	client_config.stream.read_buffer_size = 0;
	RIWO_TEST_CHECK_THROWS(ws::client(client_config), std::invalid_argument);
	client_config.stream.read_buffer_size = ws::stream_config{}.read_buffer_size;
	client_config.stream.ping_interval = std::chrono::milliseconds(-1);
	RIWO_TEST_CHECK_THROWS(ws::client(client_config), std::invalid_argument);
	client_config.stream.ping_interval =
		ws::stream_config{}.ping_interval;

	asio::ip::tcp::acceptor acceptor(context);
	ws::server_config server_config;
	server_config.default_upgrade.stream.read_buffer_size = 0;
	RIWO_TEST_CHECK_THROWS(
		ws::server(std::move(acceptor), server_config), std::invalid_argument);

	asio::ip::tcp::acceptor ping_acceptor(context);
	server_config.default_upgrade.stream.read_buffer_size =
		ws::stream_config{}.read_buffer_size;
	server_config.default_upgrade.stream.ping_interval =
		std::chrono::milliseconds(-1);
	RIWO_TEST_CHECK_THROWS(
		ws::server(std::move(ping_acceptor), server_config), std::invalid_argument);

}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"delivery modes and cancellation", delivery_modes_and_cancellation},
		{"pending handshake queue", pending_handshake_queue},
		{"unavailable handshake queue", unavailable_handshake_queue},
		{"client cancellation", owned_client_cancellation},
		{"client handshake timeout", owned_client_handshake_timeout},
		{"pending handshake timeout", pending_handshake_timeout},
		{"pending handshake FIFO and capacity",
			pending_handshake_fifo_and_capacity},
		{"simultaneous close", simultaneous_close},
		{"explicit retry open recovery", explicit_retry_open_recovery},
		{"retry open cancellation", retry_open_cancellation},
		{"invalid configuration", invalid_owned_config},
	});
}

