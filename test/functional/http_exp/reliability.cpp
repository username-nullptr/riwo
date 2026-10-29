// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/http_exp/transport/detail/timeout.h>
#include <riwo/core/cxx/asio.h>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] std::string body_text(const http::reply &reply)
{
	const auto bytes = reply.buffered_body();
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void transport_timeout_state_machine()
{
	struct owner
	{
		void cancel() noexcept
		{
			++cancel_count;
			if( on_cancel )
				on_cancel();
		}

		size_t cancel_count = 0;
		std::function<void()> on_cancel;
	};

	riwo::io_context_t context;
	auto value = std::make_shared<owner>();
	bool completed = false;
	http::detail::start_timed_io(value, context.get_executor(),
		std::chrono::milliseconds(100),
		http::connection::io_handler_t(
		[&](riwo::error_code error, size_t size) {
			RIWO_TEST_CHECK(!error);
			RIWO_TEST_CHECK_EQ(size, 3U);
			completed = true;
		}),
		[&](auto completion) {
			asio::post(context,
			[completion = std::move(completion)]() mutable {
				std::move(completion)(riwo::error_code{}, 3);
			});
		});
	context.run();
	RIWO_TEST_CHECK(completed);
	RIWO_TEST_CHECK_EQ(value->cancel_count, 0U);

	context.restart();
	asio::steady_timer pending(context);
	pending.expires_after(std::chrono::seconds(1));
	value->on_cancel = [&] { (void)pending.cancel(); };
	bool timed_out = false;
	http::detail::start_timed_io(value, context.get_executor(),
		std::chrono::milliseconds(5),
		http::connection::io_handler_t(
		[&](riwo::error_code error, size_t size) {
			RIWO_TEST_CHECK_EQ(error,
				asio::error::make_error_code(asio::error::timed_out));
			RIWO_TEST_CHECK_EQ(size, 0U);
			timed_out = true;
		}),
		[&](auto completion) {
			pending.async_wait(
			[completion = std::move(completion)]
			(riwo::error_code error) mutable {
				std::move(completion)(error, 0);
			});
		});
	context.run();
	RIWO_TEST_CHECK(timed_out);
	RIWO_TEST_CHECK_EQ(value->cancel_count, 1U);
}

void buffered_limit_recovery_for(http::protocol_requirement requirement,
	http::protocol expected_protocol)
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement = requirement;
	http::server service(std::move(acceptor), server_config);
	std::atomic_size_t large_requests {0};
	std::atomic_size_t recovery_requests {0};
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/large",
		[&](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			large_requests.fetch_add(1, std::memory_order_relaxed);
			co_await exchange.respond({http::status::ok, {}},
				std::string(32 * 1024, 'l'), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/recover",
		[&](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			recovery_requests.fetch_add(1, std::memory_order_relaxed);
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("recovered"), riwo::use_awaitable);
		})
		// The limited client intentionally abandons the first response.  That is
		// an expected per-exchange failure, not a test-server failure.
		.on_server_error([](riwo::error_code) { return true; })
		.on_service_error([](http::server_exchange &,
			const std::exception&) { return true; })
		.start();

	const auto base = std::format("http://127.0.0.1:{}",
		service.acceptor().local_endpoint().port());
	http::client_config client_config;
	client_config.protocols.requirement = requirement;
	client_config.pool.max_sessions_per_origin = 1;
	client_config.retries.max_attempts = 1;
	client_config.reply_limits.max_buffered_body = 1024;
	http::client client(context, client_config);

	std::jthread runner([&] { context.run(); });
	try
	{
		riwo::error_code error;
		(void)client.fetch(http::request(http::method::get, base + "/large"),
			error);
		RIWO_TEST_CHECK_EQ(error,
			http::make_error_code(http::errc::buffer_limit_exceeded));

		error.clear();
		auto recovered = client.fetch(http::request(http::method::get,
			base + "/recover"), error);
		RIWO_TEST_CHECK(!error);
		RIWO_TEST_CHECK_EQ(recovered.selected_protocol(), expected_protocol);
		RIWO_TEST_CHECK_EQ(recovered.status(), http::status::ok);
		RIWO_TEST_CHECK_EQ(body_text(recovered), "recovered");
		RIWO_TEST_CHECK_EQ(large_requests.load(std::memory_order_relaxed), 1U);
		RIWO_TEST_CHECK_EQ(recovery_requests.load(std::memory_order_relaxed), 1U);
	}
	catch(...)
	{
		service.stop();
		context.stop();
		runner.join();
		throw;
	}
	service.stop();
	context.stop();
	runner.join();
}

void buffered_limit_recovery_http1()
{
	buffered_limit_recovery_for(http::protocol_requirement::v1,
		http::protocol::v1_1);
}

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
void buffered_limit_recovery_http2()
{
	buffered_limit_recovery_for(http::protocol_requirement::v2,
		http::protocol::v2);
}
#endif

void header_timeout_and_recovery()
{
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement =
		http::protocol_requirement::v1;
	http::server service(std::move(acceptor), server_config);
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/slow-head",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto executor = co_await asio::this_coro::executor;
			asio::steady_timer delay(executor);
			delay.expires_after(std::chrono::milliseconds(200));
			co_await delay.async_wait(riwo::use_awaitable);
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("late"), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/recover",
		[](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::ok, {}},
				std::string_view("ready"), riwo::use_awaitable);
		})
		.on_server_error([](riwo::error_code) { return true; })
		.on_service_error([](http::server_exchange &,
			const std::exception&) { return true; })
		.start();

	const auto base = std::format("http://127.0.0.1:{}",
		service.acceptor().local_endpoint().port());
	http::client_config client_config;
	client_config.protocols.requirement =
		http::protocol_requirement::v1;
	client_config.timeout.header = std::chrono::milliseconds(40);
	client_config.timeout.body_idle = std::chrono::milliseconds(500);
	client_config.pool.max_sessions_per_origin = 1;
	client_config.retries.max_attempts = 1;
	http::client client(context, client_config);

	std::jthread runner([&] { context.run(); });
	try
	{
		riwo::error_code error;
		(void)client.fetch(http::request(http::method::get,
			base + "/slow-head"), error);
		RIWO_TEST_CHECK_EQ(error,
			asio::error::make_error_code(asio::error::timed_out));

		error.clear();
		auto recovered = client.fetch(http::request(http::method::get,
			base + "/recover"), error);
		RIWO_TEST_CHECK(!error);
		RIWO_TEST_CHECK_EQ(recovered.selected_protocol(), http::protocol::v1_1);
		RIWO_TEST_CHECK_EQ(body_text(recovered), "ready");
	}
	catch(...)
	{
		service.stop();
		context.stop();
		runner.join();
		throw;
	}
	service.stop();
	context.stop();
	runner.join();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"transport timeout state machine", transport_timeout_state_machine},
		{"HTTP/1 buffered limit recovery", buffered_limit_recovery_http1},
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		{"HTTP/2 buffered limit recovery", buffered_limit_recovery_http2},
#endif
		{"HTTP/1 header timeout recovery", header_timeout_and_recovery}
	});
}
