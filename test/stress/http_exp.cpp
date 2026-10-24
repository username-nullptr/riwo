// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http_exp.h>
#include <riwo/core/cxx/asio.h>
#include <algorithm>
#include <array>
#include <future>
#include <span>

namespace
{

namespace http = riwo::http_exp;

[[nodiscard]] std::string_view body_view(const http::reply &reply)
{
	const auto bytes = reply.buffered_body();
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void rethrow_after_cleanup(std::vector<std::future<void>> &futures,
	http::server &service, riwo::io_context_t &context,
	std::array<std::thread,4> &runners)
{
	std::exception_ptr failure;
	for(auto &future : futures)
	{
		try {
			future.get();
		}
		catch(...) {
			if( !failure ) failure = std::current_exception();
		}
	}
	service.stop();
	context.stop();
	for(auto &runner : runners)
		runner.join();
	if( failure )
		std::rethrow_exception(failure);
}

void concurrent_http1_pool_pressure()
{
	constexpr size_t worker_count = 8;
	const size_t requests_per_worker = 40 * RIWO_STRESS_SCALE;
	const auto seed = riwo::test::current_seed();
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement =
		http::protocol_requirement::require_http1;
	http::server service(std::move(acceptor), server_config);
	std::atomic_size_t handled {0};
	std::atomic_size_t server_errors {0};
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/pressure",
		[&](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			const auto size_text =
				exchange.query_parameter("size").value_or("0");
			size_t body_size = 0;
			const auto parsed = std::from_chars(size_text.data(),
				size_text.data() + size_text.size(), body_size);
			if( parsed.ec != std::errc{} or
				parsed.ptr != size_text.data() + size_text.size() or
				body_size > 64 * 1024 )
				throw std::invalid_argument("invalid stress response size");
			const auto fill_text =
				exchange.query_parameter("fill").value_or("x");
			if( fill_text.size() != 1 )
				throw std::invalid_argument("invalid stress response fill");
			http::response_head head;
			head.headers.add("X-Stress-Size", std::to_string(body_size));
			handled.fetch_add(1, std::memory_order_relaxed);
			co_await exchange.respond(std::move(head),
				std::string(body_size, fill_text.front()), riwo::use_awaitable);
		})
		.on_server_error([&](riwo::error_code) {
			server_errors.fetch_add(1, std::memory_order_relaxed);
			return true;
		})
		.on_service_error([&](http::server_exchange &,
			const std::exception&) {
			server_errors.fetch_add(1, std::memory_order_relaxed);
			return true;
		})
		.start();

	const auto target = std::format("http://127.0.0.1:{}/pressure",
		service.acceptor().local_endpoint().port());
	http::client_config client_config;
	client_config.protocols.requirement =
		http::protocol_requirement::require_http1;
	client_config.pool.max_sessions_per_origin = 4;
	client_config.pool.max_waiters_per_origin = worker_count * 2;
	client_config.retries.max_attempts = 1;
	auto client_strand = asio::make_strand(context);
	http::client client(client_strand, client_config);

	std::vector<std::future<void>> futures;
	futures.reserve(worker_count);
	for(size_t worker = 0; worker < worker_count; ++worker)
	{
		futures.emplace_back(asio::co_spawn(client_strand,
		[&, worker, seed]() -> riwo::awaitable<void>
		{
			for(size_t request_index = 0;
				request_index < requests_per_worker; ++request_index)
			{
				const auto body_size = request_index % 19 == 0 ?
					size_t {64 * 1024} :
					size_t {31 + (worker * 97 + request_index * 13) % 997};
				const auto fill = static_cast<char>('a' +
					(worker * 7 + request_index) % 26);
				const auto url = std::format("{}?size={}&fill={}",
					target, body_size, fill);
				auto reply = co_await client.fetch(
					http::request(http::method::get, url), riwo::use_awaitable);
				RIWO_TEST_CHECK_EQ(reply.selected_protocol(), http::protocol::v1_1);
				RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
				RIWO_TEST_CHECK_EQ(reply.headers().find_first("x-stress-size"),
					std::to_string(body_size));
				const auto body = body_view(reply);
				RIWO_TEST_CHECK_EQ(body.size(), body_size);
				RIWO_TEST_CHECK(std::ranges::all_of(body,
					[fill](char value) { return value == fill; }));
				if( ((seed ^ worker ^ request_index) & 7U) == 0 )
					co_await asio::post(riwo::use_awaitable);
			}
		}, riwo::use_future));
	}

	std::array<std::thread,4> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	rethrow_after_cleanup(futures, service, context, runners);
	RIWO_TEST_CHECK_EQ(handled.load(std::memory_order_relaxed),
		worker_count * requests_per_worker);
	RIWO_TEST_CHECK_EQ(server_errors.load(std::memory_order_relaxed), 0U);
}

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
void http2_multiplexed_slow_consumer_pressure()
{
	const size_t fast_count = 12 * RIWO_STRESS_SCALE;
	const auto seed = riwo::test::current_seed();
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement =
		http::protocol_requirement::require_http2;
	// 128 default-sized stream windows fit inside the configured 8 MiB
	// connection budget; raising only the stream count would be an invalid
	// resource configuration rather than useful pressure.
	server_config.h2.max_concurrent_streams = 128;
	http::server service(std::move(acceptor), server_config);
	std::atomic_size_t fast_handled {0};
	std::atomic_bool slow_finished {false};
	std::atomic_size_t server_errors {0};
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/slow",
		[&](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond({http::status::ok, {}},
				http::body_spec::streaming(), riwo::use_awaitable);
			const std::string chunk(4096, 's');
			for(size_t index = 0; index < 128; ++index)
				(void)co_await exchange.response_body().write(
					riwo::buffer(chunk), riwo::use_awaitable);
			co_await exchange.response_body().finish({}, riwo::use_awaitable);
			slow_finished.store(true, std::memory_order_release);
		})
		.on_request<http::method::get>("/fast/{id}",
		[&](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			const auto id = exchange.path_parameter("id").value_or("missing");
			const auto body = std::string("fast-") + std::string(id);
			fast_handled.fetch_add(1, std::memory_order_relaxed);
			co_await exchange.respond({http::status::ok, {}}, body,
				riwo::use_awaitable);
		})
		.on_server_error([&](riwo::error_code) {
			server_errors.fetch_add(1, std::memory_order_relaxed);
			return true;
		})
		.on_service_error([&](http::server_exchange &,
			const std::exception&) {
			server_errors.fetch_add(1, std::memory_order_relaxed);
			return true;
		})
		.start();

	const auto base = std::format("http://127.0.0.1:{}",
		service.acceptor().local_endpoint().port());
	http::client_config client_config;
	client_config.protocols.requirement =
		http::protocol_requirement::require_http2;
	client_config.pool.max_sessions_per_origin = 1;
	client_config.pool.max_waiters_per_origin = fast_count + 4;
	client_config.retries.max_attempts = 1;
	client_config.h2.max_concurrent_streams = 128;
	auto client_strand = asio::make_strand(context);
	http::client client(client_strand, client_config);

	std::vector<std::future<void>> completed;
	completed.emplace_back(asio::co_spawn(client_strand,
	[&, seed]() -> riwo::awaitable<void>
	{
		http::reply slow;
		try {
			slow = co_await client.send(http::request(http::method::get,
				base + "/slow"), riwo::use_awaitable);
		}
		catch(const std::exception &error) {
			throw std::runtime_error(std::string("open slow stream: ") +
				error.what());
		}
		RIWO_TEST_CHECK_EQ(slow.selected_protocol(), http::protocol::v2);

		std::vector<std::future<void>> fast;
		fast.reserve(fast_count);
		for(size_t index = 0; index < fast_count; ++index)
		{
			fast.emplace_back(asio::co_spawn(client_strand,
			[&, index, seed]() -> riwo::awaitable<void>
			{
				try
				{
					const auto expected = "fast-" + std::to_string(index);
					auto reply = co_await client.fetch(http::request(http::method::get,
						base + "/fast/" + std::to_string(index)),
						riwo::use_awaitable);
					RIWO_TEST_CHECK_EQ(reply.selected_protocol(), http::protocol::v2);
					RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
					RIWO_TEST_CHECK_EQ(body_view(reply), expected);
				}
				catch(const std::exception &error) {
					throw std::runtime_error("fast stream " +
						std::to_string(index) + ": " + error.what());
				}
				if( ((seed ^ index) & 3U) == 0 )
					co_await asio::post(riwo::use_awaitable);
			}, riwo::use_future));
		}

		asio::steady_timer wait_timer(co_await asio::this_coro::executor);
		for(auto &future : fast)
		{
			while(future.wait_for(std::chrono::milliseconds(0)) !=
				std::future_status::ready)
			{
				wait_timer.expires_after(std::chrono::milliseconds(1));
				co_await wait_timer.async_wait(riwo::use_awaitable);
			}
			future.get();
		}
		RIWO_TEST_CHECK_EQ(fast_handled.load(std::memory_order_relaxed),
			fast_count);

		std::array<char,1021> buffer {};
		size_t received = 0;
		for(size_t reads = 0;; ++reads)
		{
			http::body_read_result result;
			try {
				result = co_await slow.body().read_some(
					riwo::buffer(buffer), riwo::use_awaitable);
			}
			catch(const std::exception &error) {
				throw std::runtime_error(std::string("read slow stream: ") +
					error.what());
			}
			received += result.bytes;
			RIWO_TEST_CHECK(std::ranges::all_of(
				std::span(buffer.data(), result.bytes),
				[](char value) { return value == 's'; }));
			if( result.eof ) break;
			if( (reads & 7U) == 0 )
				co_await asio::post(riwo::use_awaitable);
		}
		RIWO_TEST_CHECK_EQ(received, 512U * 1024U);
		for(size_t attempt = 0; attempt < 100 and
			!slow_finished.load(std::memory_order_acquire); ++attempt)
			co_await asio::post(riwo::use_awaitable);
		RIWO_TEST_CHECK(slow_finished.load(std::memory_order_acquire));
	}, riwo::use_future));

	std::array<std::thread,4> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	rethrow_after_cleanup(completed, service, context, runners);
	RIWO_TEST_CHECK_EQ(server_errors.load(std::memory_order_relaxed), 0U);
}
#endif

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"concurrent HTTP_EXP HTTP/1 pool pressure",
			concurrent_http1_pool_pressure},
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		{"HTTP_EXP HTTP/2 multiplexed slow consumer pressure",
			http2_multiplexed_slow_consumer_pressure}
#endif
	});
}
