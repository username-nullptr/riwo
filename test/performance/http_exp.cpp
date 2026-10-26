// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/http_exp.h>

#ifndef RIWO_HTTP_EXP_COMPARE_LEGACY
# define RIWO_HTTP_EXP_COMPARE_LEGACY 0
#endif

#if RIWO_HTTP_EXP_COMPARE_LEGACY
# include <riwo/http/client.h>
# include <riwo/http/server.h>
# include <riwo/http/cxx/configs.h>
# include <riwo/http/protocol/utils/client/generator.h>
# include <riwo/http/protocol/utils/client/parser.h>
# include <riwo/http/protocol/utils/core/compression.h>
# include <riwo/http/protocol/utils/server/generator.h>
# include <riwo/http/protocol/utils/server/parser.h>
#endif

#include <algorithm>
#include <array>
#include <future>
#include <optional>

namespace
{

#ifdef NDEBUG
constexpr size_t warmup_count = 100;
constexpr size_t request_count = 2'000 * riwo::test::performance_scale;
constexpr size_t protocol_cycle_count =
	250'000 * riwo::test::performance_scale;
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
constexpr size_t gzip_cycle_count = 2'000 * riwo::test::performance_scale;
constexpr size_t small_gzip_cycle_count =
	10'000 * riwo::test::performance_scale;
#endif
#else
constexpr size_t warmup_count = 10;
constexpr size_t request_count = 200 * riwo::test::performance_scale;
constexpr size_t protocol_cycle_count =
	10'000 * riwo::test::performance_scale;
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
constexpr size_t gzip_cycle_count = 100 * riwo::test::performance_scale;
constexpr size_t small_gzip_cycle_count =
	500 * riwo::test::performance_scale;
#endif
#endif
constexpr size_t secondary_request_count = request_count / 10;
constexpr size_t keep_alive_batch_size = 50;
constexpr size_t concurrent_client_count = 8;
constexpr size_t concurrent_io_thread_count = 4;

using duration_t = std::chrono::steady_clock::duration;

template <typename Measure>
riwo::awaitable<duration_t> median_async_duration(
	Measure &measure, size_t iterations, size_t batch_size)
{
	co_await measure(std::min(warmup_count, iterations), batch_size);
	std::array<duration_t,3> samples {};
	for(auto &sample : samples)
		sample = co_await measure(iterations, batch_size);
	std::ranges::sort(samples);
	co_return samples[1];
}

template <typename Measure>
duration_t median_duration(size_t iterations, Measure &&measure)
{
	measure(std::max<size_t>(1, iterations / 20));
	std::array<duration_t,3> samples {};
	for(auto &sample : samples)
		sample = measure(iterations);
	std::ranges::sort(samples);
	return samples[1];
}

template <typename First, typename Second, typename Third>
std::tuple<duration_t,duration_t,duration_t> balanced_median_durations(
	size_t iterations, First &&first, Second &&second, Third &&third)
{
	const auto warmup = std::max<size_t>(1, iterations / 20);
	first(warmup);
	second(warmup);
	third(warmup);
	std::array<duration_t,3> first_samples {};
	std::array<duration_t,3> second_samples {};
	std::array<duration_t,3> third_samples {};
	first_samples[0] = first(iterations);
	second_samples[0] = second(iterations);
	third_samples[0] = third(iterations);
	second_samples[1] = second(iterations);
	third_samples[1] = third(iterations);
	first_samples[1] = first(iterations);
	third_samples[2] = third(iterations);
	first_samples[2] = first(iterations);
	second_samples[2] = second(iterations);
	std::ranges::sort(first_samples);
	std::ranges::sort(second_samples);
	std::ranges::sort(third_samples);
	return {first_samples[1], second_samples[1], third_samples[1]};
}

[[nodiscard]] std::string_view body_view(const riwo::http_exp::reply &reply)
{
	const auto bytes = reply.buffered_body();
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

struct loopback_results
{
	duration_t small_keep_alive {};
	duration_t large_keep_alive {};
	duration_t small_reconnect {};
	size_t connection_recoveries = 0;
};

#if RIWO_HTTP_EXP_COMPARE_LEGACY
void print_comparison(std::string_view name, size_t operations,
	duration_t legacy, duration_t experimental, std::string_view unit);
#endif

loopback_results measure_http_exp_loopback()
{
	namespace http = riwo::http_exp;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement = http::protocol_requirement::v1;
	server_config.compression.enabled = false;
	http::server service(std::move(acceptor), server_config);
	const std::string small_body = "ok";
	const std::string large_body(64 * 1024, 'x');
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/benchmark",
		[&small_body](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond(http::response_head{},
				riwo::buffer(small_body), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/benchmark-large",
		[&large_body](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond(http::response_head{},
				riwo::buffer(large_body), riwo::use_awaitable);
		})
		.start();

	const auto port = service.acceptor().local_endpoint().port();
	RIWO_TEST_CHECK(port != 0);
	const auto url = std::format("http://127.0.0.1:{}/benchmark", port);
	const auto large_url = std::format(
		"http://127.0.0.1:{}/benchmark-large", port);
	http::client_config client_config;
	client_config.protocols.requirement = http::protocol_requirement::v1;
	client_config.pool.max_sessions_per_origin = 1;
	client_config.pool.idle_timeout = std::chrono::milliseconds(250);
	client_config.retries.max_attempts = 2;
	client_config.auto_decompression = false;
	loopback_results results;
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		try
		{
			auto measure = [&](const std::string &target,
				std::string_view expected_body)
			{
				return [&, target, expected_body](size_t iterations,
					size_t batch_size) -> riwo::awaitable<duration_t>
				{
					RIWO_TEST_CHECK(batch_size > 0);
					std::optional<http::client> requester;
					const auto begin = std::chrono::steady_clock::now();
					for(size_t index = 0; index < iterations; ++index)
					{
						if( not requester )
							requester.emplace(
								context.get_executor(), client_config);
						auto reply = co_await requester->fetch(
							http::request(http::method::get, target),
							riwo::use_awaitable);
						RIWO_TEST_CHECK_EQ(reply.selected_protocol(),
							http::protocol::v1_1);
						RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
						RIWO_TEST_CHECK_EQ(body_view(reply), expected_body);
						if( (index + 1) % batch_size == 0 )
							requester.reset();
					}
					requester.reset();
					co_return std::chrono::steady_clock::now() - begin;
				};
			};

			auto small = measure(url, small_body);
			auto large = measure(large_url, large_body);
			results.small_keep_alive = co_await median_async_duration(
				small, request_count, keep_alive_batch_size);
			results.large_keep_alive = co_await median_async_duration(
				large, secondary_request_count, keep_alive_batch_size);
			results.small_reconnect = co_await median_async_duration(
				small, secondary_request_count, 1);
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
	return results;
}

#if RIWO_HTTP_EXP_COMPARE_LEGACY
loopback_results measure_legacy_http_loopback()
{
	namespace http = riwo::http;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	const std::string small_body = "ok";
	const std::string large_body(64 * 1024, 'x');
	auto server_config = service.config();
	server_config.keepalive_time = std::chrono::seconds(30);
	service.set_config(server_config);
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/benchmark",
		[&small_body](http::server::context_t &request_context)
			-> riwo::awaitable<void>
		{
			request_context.response().set_auto_compression(false);
			co_await request_context.response().write(
				asio::buffer(small_body), riwo::use_awaitable);
		})
		.on_request<http::method::get>("/benchmark-large",
		[&large_body](http::server::context_t &request_context)
			-> riwo::awaitable<void>
		{
			request_context.response().set_auto_compression(false);
			co_await request_context.response().write(
				asio::buffer(large_body), riwo::use_awaitable);
		})
		.start();

	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	RIWO_TEST_CHECK(port != 0);
	const auto url = std::format("http://127.0.0.1:{}/benchmark", port);
	const auto large_url = std::format(
		"http://127.0.0.1:{}/benchmark-large", port);
	loopback_results results;
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		try
		{
			auto measure = [&](const std::string &target,
				std::string_view expected_body)
			{
				return [&, target, expected_body](size_t iterations,
					size_t batch_size) -> riwo::awaitable<duration_t>
				{
					RIWO_TEST_CHECK(batch_size > 0);
					std::optional<http::client> requester;
					const auto begin = std::chrono::steady_clock::now();
					for(size_t index = 0; index < iterations; ++index)
					{
						for(size_t attempt = 0; attempt != 2; ++attempt)
						{
							try
							{
								if( not requester )
									requester.emplace(context.get_executor());
								auto request = co_await requester->request_get(
									http::client::req_info(target),
									riwo::use_awaitable);
								RIWO_TEST_CHECK(request);
								RIWO_TEST_CHECK_EQ(
									co_await request->wait_reply(riwo::use_awaitable),
									http::status::ok);
								RIWO_TEST_CHECK_EQ(
									co_await request->reply()->read<std::string>(
										riwo::use_awaitable), expected_body);
								break;
							}
							catch(const std::exception &error)
							{
								requester.reset();
								if( attempt != 0 )
									throw std::runtime_error(std::format(
										"legacy HTTP request {} of {}: {}",
										index + 1, iterations, error.what()));
								++results.connection_recoveries;
							}
						}
						if( (index + 1) % batch_size == 0 )
							requester.reset();
					}
					requester.reset();
					co_return std::chrono::steady_clock::now() - begin;
				};
			};

			auto small = measure(url, small_body);
			auto large = measure(large_url, large_body);
			results.small_keep_alive = co_await median_async_duration(
				small, request_count, keep_alive_batch_size);
			results.large_keep_alive = co_await median_async_duration(
				large, secondary_request_count, keep_alive_batch_size);
			results.small_reconnect = co_await median_async_duration(
				small, secondary_request_count, 1);
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
	return results;
}
#endif

void http_exp_loopback_throughput()
{
#if RIWO_HTTP_EXP_COMPARE_LEGACY
	const auto legacy = measure_legacy_http_loopback();
	riwo::test::print_performance_result(
		"HTTP legacy/HTTP/1.1 keep-alive (50 requests/connection), 2 B body",
		request_count, legacy.small_keep_alive, "request");
	riwo::test::print_performance_result(
		"HTTP legacy/HTTP/1.1 keep-alive (50 requests/connection), 64 KiB body",
		secondary_request_count, legacy.large_keep_alive, "request");
	riwo::test::print_performance_result(
		"HTTP legacy/HTTP/1.1 new connection, 2 B body",
		secondary_request_count, legacy.small_reconnect, "request");
	std::cout << "[PERF] HTTP legacy connection recoveries across warmup and "
		"measured samples: " << legacy.connection_recoveries << '\n'
		<< std::flush;
#endif

	const auto experimental = measure_http_exp_loopback();
	riwo::test::print_performance_result(
		"HTTP_EXP/HTTP/1.1 keep-alive (50 requests/connection), 2 B body",
		request_count, experimental.small_keep_alive, "request");
	riwo::test::print_performance_result(
		"HTTP_EXP/HTTP/1.1 keep-alive (50 requests/connection), 64 KiB body",
		secondary_request_count, experimental.large_keep_alive, "request");
	riwo::test::print_performance_result(
		"HTTP_EXP/HTTP/1.1 new connection, 2 B body",
		secondary_request_count, experimental.small_reconnect, "request");

#if RIWO_HTTP_EXP_COMPARE_LEGACY
	print_comparison("HTTP/1.1 keep-alive 2 B body", request_count,
		legacy.small_keep_alive, experimental.small_keep_alive, "requests");
	print_comparison("HTTP/1.1 keep-alive 64 KiB body",
		secondary_request_count, legacy.large_keep_alive,
		experimental.large_keep_alive, "requests");
	print_comparison("HTTP/1.1 new connection 2 B body",
		secondary_request_count, legacy.small_reconnect,
		experimental.small_reconnect, "requests");
#endif
}

struct concurrent_result
{
	duration_t elapsed {};
	size_t connection_recoveries = 0;
};

concurrent_result measure_http_exp_concurrent_loopback()
{
	namespace http = riwo::http_exp;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement = http::protocol_requirement::v1;
	server_config.compression.enabled = false;
	http::server service(std::move(acceptor), server_config);
	const std::string body = "ok";
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/benchmark",
		[&body](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			co_await exchange.respond(http::response_head{},
				riwo::buffer(body), riwo::use_awaitable);
		})
		.start();

	const auto target = std::format("http://127.0.0.1:{}/benchmark",
		service.acceptor().local_endpoint().port());
	http::client_config client_config;
	client_config.protocols.requirement = http::protocol_requirement::v1;
	client_config.pool.max_sessions_per_origin = 1;
	client_config.pool.idle_timeout = std::chrono::milliseconds(250);
	client_config.retries.max_attempts = 2;
	client_config.auto_decompression = false;

	std::array<std::thread,concurrent_io_thread_count> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	auto cleanup = [&]
	{
		service.stop();
		context.stop();
		for(auto &runner : runners)
			runner.join();
	};

	concurrent_result result;
	try
	{
		auto measure = [&](size_t total)
		{
			std::vector<std::future<void>> completed;
			completed.reserve(concurrent_client_count);
			const auto begin = std::chrono::steady_clock::now();
			for(size_t worker = 0; worker < concurrent_client_count; ++worker)
			{
				const auto iterations = total / concurrent_client_count +
					(worker < total % concurrent_client_count ? 1U : 0U);
				completed.emplace_back(asio::co_spawn(context,
				[&, iterations]() -> riwo::awaitable<void>
				{
					http::client requester(context.get_executor(), client_config);
					for(size_t index = 0; index < iterations; ++index)
					{
						auto reply = co_await requester.fetch(
							http::request(http::method::get, target),
							riwo::use_awaitable);
						RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
						RIWO_TEST_CHECK_EQ(body_view(reply), body);
					}
				}, riwo::use_future));
			}
			for(auto &future : completed)
				future.get();
			return std::chrono::steady_clock::now() - begin;
		};
		result.elapsed = median_duration(request_count, measure);
	}
	catch(...)
	{
		cleanup();
		throw;
	}
	cleanup();
	return result;
}

#if RIWO_HTTP_EXP_COMPARE_LEGACY
concurrent_result measure_legacy_http_concurrent_loopback()
{
	namespace http = riwo::http;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server service(std::move(acceptor));
	const std::string body = "ok";
	auto server_config = service.config();
	server_config.keepalive_time = std::chrono::seconds(30);
	service.set_config(server_config);
	service.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/benchmark",
		[&body](http::server::context_t &request_context)
			-> riwo::awaitable<void>
		{
			request_context.response().set_auto_compression(false);
			co_await request_context.response().write(
				asio::buffer(body), riwo::use_awaitable);
		})
		.start();

	const auto target = std::format("http://127.0.0.1:{}/benchmark",
		service.acceptor_wrap().acceptor().local_endpoint().port());
	std::atomic_size_t connection_recoveries {0};
	std::array<std::thread,concurrent_io_thread_count> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	auto cleanup = [&]
	{
		service.stop();
		context.stop();
		for(auto &runner : runners)
			runner.join();
	};

	concurrent_result result;
	try
	{
		auto measure = [&](size_t total)
		{
			std::vector<std::future<void>> completed;
			completed.reserve(concurrent_client_count);
			const auto begin = std::chrono::steady_clock::now();
			for(size_t worker = 0; worker < concurrent_client_count; ++worker)
			{
				const auto iterations = total / concurrent_client_count +
					(worker < total % concurrent_client_count ? 1U : 0U);
				completed.emplace_back(asio::co_spawn(context,
				[&, iterations]() -> riwo::awaitable<void>
				{
					http::client requester(context.get_executor());
					for(size_t index = 0; index < iterations; ++index)
					{
						for(size_t attempt = 0; attempt != 2; ++attempt)
						{
							try
							{
								auto request = co_await requester.request_get(
									http::client::req_info(target),
									riwo::use_awaitable);
								RIWO_TEST_CHECK(request);
								RIWO_TEST_CHECK_EQ(co_await request->wait_reply(
									riwo::use_awaitable), http::status::ok);
								RIWO_TEST_CHECK_EQ(co_await request->reply()
									->read<std::string>(riwo::use_awaitable), body);
								break;
							}
							catch(const std::system_error&)
							{
								if( attempt != 0 )
									throw;
								requester = http::client(context.get_executor());
								connection_recoveries.fetch_add(
									1, std::memory_order_relaxed);
							}
						}
					}
				}, riwo::use_future));
			}
			for(auto &future : completed)
				future.get();
			return std::chrono::steady_clock::now() - begin;
		};
		result.elapsed = median_duration(request_count, measure);
		result.connection_recoveries = connection_recoveries.load(
			std::memory_order_relaxed);
	}
	catch(...)
	{
		cleanup();
		throw;
	}
	cleanup();
	return result;
}
#endif

void http_exp_concurrent_loopback_throughput()
{
#if RIWO_HTTP_EXP_COMPARE_LEGACY
	const auto legacy = measure_legacy_http_concurrent_loopback();
	riwo::test::print_performance_result(
		"HTTP legacy/HTTP/1.1 concurrent (8 clients, 4 I/O threads), 2 B body",
		request_count, legacy.elapsed, "request");
	std::cout << "[PERF] HTTP legacy concurrent connection recoveries across "
		"warmup and measured samples: " << legacy.connection_recoveries << '\n'
		<< std::flush;
#endif

	const auto experimental = measure_http_exp_concurrent_loopback();
	riwo::test::print_performance_result(
		"HTTP_EXP/HTTP/1.1 concurrent (8 clients, 4 I/O threads), 2 B body",
		request_count, experimental.elapsed, "request");

#if RIWO_HTTP_EXP_COMPARE_LEGACY
	print_comparison("HTTP/1.1 concurrent 2 B body", request_count,
		legacy.elapsed, experimental.elapsed, "requests");
#endif
}

duration_t measure_http_exp_request_parser(size_t count)
{
	namespace http = riwo::http_exp;
	constexpr std::string_view request =
		"GET /api/items?q=42 HTTP/1.1\r\n"
		"Host: example.test\r\n"
		"Accept: text/plain\r\n"
		"X-Benchmark: comparison\r\n\r\n";
	http::v1::request_parser parser;
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto parsed = parser.ingest(riwo::buffer(request));
		RIWO_TEST_CHECK(parsed and parsed->head);
		RIWO_TEST_CHECK_EQ(parsed->consumed, request.size());
		checksum += parsed->head->head.target.size();
		parser.reset();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(checksum,
		count * std::string_view("/api/items?q=42").size());
	return elapsed;
}

duration_t measure_http_exp_request_generator(size_t count)
{
	namespace http = riwo::http_exp;
	http::request_head head;
	head.method = http::method::post;
	head.scheme = "http";
	head.authority = "example.test";
	head.target = "/api/items?q=42";
	head.headers.add("Content-Type", "text/plain");
	constexpr size_t body_size = 16;
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto output = http::v1::serialize_request_head(head,
			http::body_spec::known(body_size), http::protocol::v1_1);
		RIWO_TEST_CHECK(output);
		checksum += output->wire.size();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum > count * body_size);
	return elapsed;
}

duration_t measure_http_exp_response_parser(size_t count)
{
	namespace http = riwo::http_exp;
	constexpr std::string_view response =
		"HTTP/1.1 204 No Content\r\n"
		"Server: riwo-benchmark\r\n"
		"X-Benchmark: comparison\r\n\r\n";
	http::v1::response_parser parser;
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto parsed = parser.ingest(riwo::buffer(response));
		RIWO_TEST_CHECK(parsed and parsed->head);
		RIWO_TEST_CHECK_EQ(parsed->consumed, response.size());
		checksum += parsed->head->head.status.code();
		parser.reset();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(checksum, count * size_t {204});
	return elapsed;
}

duration_t measure_http_exp_response_generator(size_t count)
{
	namespace http = riwo::http_exp;
	http::response_head head;
	head.status = http::status::ok;
	head.headers.add("Content-Type", "text/plain");
	constexpr size_t body_size = 16;
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto output = http::v1::serialize_response_head(head,
			http::body_spec::known(body_size), http::protocol::v1_1);
		RIWO_TEST_CHECK(output);
		checksum += output->wire.size();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum > count * body_size);
	return elapsed;
}

#if RIWO_HTTP_EXP_ZLIB_SUPPORT
duration_t measure_http_exp_gzip_round_trip_payload(size_t count,
	size_t payload_size)
{
	std::string payload(payload_size, 'a');
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		payload[0] = static_cast<char>(index);
		auto compressed = riwo::http_exp::gzip_compress(payload);
		RIWO_TEST_CHECK(compressed);
		auto decoded = riwo::http_exp::gzip_decompress(
			*compressed, payload.size());
		RIWO_TEST_CHECK(decoded);
		checksum += decoded->size() +
			static_cast<unsigned char>((*decoded)[0]);
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum >= count * payload.size());
	return elapsed;
}

duration_t measure_http_exp_gzip_round_trip(size_t count) {
	return measure_http_exp_gzip_round_trip_payload(count, 64 * 1024);
}

duration_t measure_http_exp_small_gzip_round_trip(size_t count) {
	return measure_http_exp_gzip_round_trip_payload(count, 512);
}

duration_t measure_http_exp_gzip_round_trip_reused_payload(size_t count,
	size_t payload_size)
{
	std::string payload(payload_size, 'a');
	riwo::http_exp::gzip_encoder encoder;
	riwo::http_exp::gzip_decoder decoder(payload.size());
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		payload[0] = static_cast<char>(index);
		auto compressed = encoder.append(payload, true);
		RIWO_TEST_CHECK(compressed);
		auto decoded = decoder.append(*compressed, true);
		RIWO_TEST_CHECK(decoded);
		checksum += decoded->size() +
			static_cast<unsigned char>((*decoded)[0]);
		RIWO_TEST_CHECK(encoder.reset());
		RIWO_TEST_CHECK(decoder.reset());
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum >= count * payload.size());
	return elapsed;
}

duration_t measure_http_exp_gzip_round_trip_reused(size_t count) {
	return measure_http_exp_gzip_round_trip_reused_payload(count, 64 * 1024);
}

duration_t measure_http_exp_small_gzip_round_trip_reused(size_t count) {
	return measure_http_exp_gzip_round_trip_reused_payload(count, 512);
}

struct compression_policy_results
{
	duration_t auto_large {};
	duration_t cached_large {};
	duration_t skipped_small {};
	duration_t forced_small {};
	duration_t skipped_precompressed {};
	duration_t forced_precompressed {};
};

compression_policy_results measure_http_exp_compression_policy_loopback()
{
	namespace http = riwo::http_exp;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	http::server_config server_config;
	server_config.protocols.requirement = http::protocol_requirement::v1;
	server_config.compression.enabled = true;
	server_config.compression.minimum_size = 1024;
	http::server service(std::move(acceptor), server_config);

	const std::string small_body(512, 's');
	const std::string large_body(64 * 1024, 'a');
	std::string precompressed_body(64 * 1024, '\0');
	uint32_t random = 0x9e3779b9U;
	for(auto &value : precompressed_body)
	{
		random ^= random << 13;
		random ^= random >> 17;
		random ^= random << 5;
		value = static_cast<char>(random);
	}
	auto cached_large_result = http::gzip_compress(large_body);
	auto forced_small_result = http::gzip_compress(small_body);
	RIWO_TEST_CHECK(cached_large_result);
	RIWO_TEST_CHECK(forced_small_result);
	const std::string cached_large = std::move(*cached_large_result);
	const size_t forced_small_size = forced_small_result->size();

	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<http::method::get>("/automatic-large",
		[&large_body](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain");
			co_await exchange.respond(std::move(head), large_body,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/cached-large",
		[&cached_large](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain");
			head.headers.add("Content-Encoding", "gzip");
			head.headers.add("Vary", "Accept-Encoding");
			co_await exchange.respond(std::move(head), cached_large,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/skipped-small",
		[&small_body](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "text/plain");
			co_await exchange.respond(std::move(head), small_body,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/forced-small",
		[&small_body](http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto encoded = http::gzip_compress(small_body);
			if( not encoded )
				throw std::system_error(encoded.error());
			http::response_head head;
			head.headers.add("Content-Type", "text/plain");
			head.headers.add("Content-Encoding", "gzip");
			co_await exchange.respond(std::move(head), *encoded,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/skipped-precompressed",
		[&precompressed_body]
		(http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			http::response_head head;
			head.headers.add("Content-Type", "application/gzip");
			co_await exchange.respond(std::move(head), precompressed_body,
				riwo::use_awaitable);
		})
		.on_request<http::method::get>("/forced-precompressed",
		[&precompressed_body]
		(http::server_exchange &exchange) -> riwo::awaitable<void>
		{
			auto encoded = http::gzip_compress(precompressed_body);
			if( not encoded )
				throw std::system_error(encoded.error());
			http::response_head head;
			head.headers.add("Content-Type", "application/gzip");
			head.headers.add("Content-Encoding", "gzip");
			co_await exchange.respond(std::move(head), *encoded,
				riwo::use_awaitable);
		})
		.start();

	const auto port = service.acceptor().local_endpoint().port();
	RIWO_TEST_CHECK(port != 0);
	const auto base = std::format("http://127.0.0.1:{}", port);
	http::client_config client_config;
	client_config.protocols.requirement = http::protocol_requirement::v1;
	client_config.pool.max_sessions_per_origin = 1;
	client_config.pool.idle_timeout = std::chrono::milliseconds(250);
	client_config.auto_decompression = false;
	compression_policy_results results;
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		try
		{
			auto measure = [&](std::string path, bool encoded,
				size_t expected_size, bool size_is_maximum = false)
			{
				return [&, path = std::move(path), encoded, expected_size,
					size_is_maximum](size_t iterations, size_t batch_size)
					-> riwo::awaitable<duration_t>
				{
					std::optional<http::client> requester;
					const auto begin = std::chrono::steady_clock::now();
					for(size_t index = 0; index < iterations; ++index)
					{
						if( not requester )
							requester.emplace(context.get_executor(), client_config);
						http::request request(http::method::get, base + path);
						request.headers().add("Accept-Encoding", "gzip");
						request.set_auto_decompression(false);
						auto reply = co_await requester->fetch(std::move(request),
							riwo::use_awaitable);
						RIWO_TEST_CHECK_EQ(reply.status(), http::status::ok);
						RIWO_TEST_CHECK_EQ(bool(reply.headers().find_first(
							"Content-Encoding")), encoded);
						const auto size = body_view(reply).size();
						if( size_is_maximum )
							RIWO_TEST_CHECK(size <= expected_size);
						else
							RIWO_TEST_CHECK_EQ(size, expected_size);
						if( (index + 1) % batch_size == 0 )
							requester.reset();
					}
					requester.reset();
					co_return std::chrono::steady_clock::now() - begin;
				};
			};

			auto auto_large = measure("/automatic-large", true,
				large_body.size(), true);
			auto cached = measure("/cached-large", true, cached_large.size());
			auto skipped_small = measure("/skipped-small", false,
				small_body.size());
			auto forced_small = measure("/forced-small", true,
				forced_small_size);
			auto skipped_precompressed = measure("/skipped-precompressed", false,
				precompressed_body.size());
			auto forced_precompressed = measure("/forced-precompressed", true,
				precompressed_body.size() + 1024, true);

			results.auto_large = co_await median_async_duration(
				auto_large, secondary_request_count, keep_alive_batch_size);
			results.cached_large = co_await median_async_duration(
				cached, secondary_request_count, keep_alive_batch_size);
			results.skipped_small = co_await median_async_duration(
				skipped_small, request_count, keep_alive_batch_size);
			results.forced_small = co_await median_async_duration(
				forced_small, request_count, keep_alive_batch_size);
			results.skipped_precompressed = co_await median_async_duration(
				skipped_precompressed, secondary_request_count,
				keep_alive_batch_size);
			results.forced_precompressed = co_await median_async_duration(
				forced_precompressed, secondary_request_count,
				keep_alive_batch_size);
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
	return results;
}
#endif

#if RIWO_HTTP_EXP_COMPARE_LEGACY
duration_t measure_legacy_request_parser(size_t count)
{
	namespace http = riwo::http;
	constexpr std::string_view request =
		"GET /api/items?q=42 HTTP/1.1\r\n"
		"Host: example.test\r\n"
		"Accept: text/plain\r\n"
		"X-Benchmark: comparison\r\n\r\n";
	http::server_parser parser(256);
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto parsed = parser.append(riwo::buffer(request));
		RIWO_TEST_CHECK(parsed and *parsed);
		checksum += parser.path().size();
		parser.reset();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(checksum,
		count * std::string_view("/api/items").size());
	return elapsed;
}

duration_t measure_legacy_request_generator(size_t count)
{
	namespace http = riwo::http;
	const riwo::url target("http://example.test/api/items?q=42");
	http::request_arg argument;
	argument.set_header("Content-Type", "text/plain");
	constexpr size_t body_size = 16;
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		http::client_generator generator(target, argument);
		auto output = generator.header_data(http::method::post, body_size);
		checksum += output.size();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum > count * body_size);
	return elapsed;
}

duration_t measure_legacy_response_parser(size_t count)
{
	namespace http = riwo::http;
	constexpr std::string_view response =
		"HTTP/1.1 204 No Content\r\n"
		"Server: riwo-benchmark\r\n"
		"X-Benchmark: comparison\r\n\r\n";
	http::client_parser parser;
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		auto parsed = parser.append(riwo::buffer(response));
		RIWO_TEST_CHECK(parsed and *parsed);
		checksum += static_cast<size_t>(parser.status());
		parser.reset();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(checksum, count * size_t {204});
	return elapsed;
}

duration_t measure_legacy_response_generator(size_t count)
{
	namespace http = riwo::http;
	constexpr size_t body_size = 16;
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		http::server_generator generator;
		generator.set_status(http::status::ok);
		generator.set_header("Content-Type", "text/plain");
		auto output = generator.header_data(body_size);
		checksum += output.size();
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum > count * body_size);
	return elapsed;
}

#if RIWO_HTTP_ZLIB_SUPPORT && RIWO_HTTP_EXP_ZLIB_SUPPORT
duration_t measure_legacy_gzip_round_trip_payload(size_t count,
	size_t payload_size)
{
	std::string payload(payload_size, 'a');
	size_t checksum = 0;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < count; ++index)
	{
		payload[0] = static_cast<char>(index);
		auto compressed = riwo::http::gzip_compress(payload);
		RIWO_TEST_CHECK(compressed);
		auto decoded = riwo::http::gzip_decompress(
			*compressed, payload.size());
		RIWO_TEST_CHECK(decoded);
		checksum += decoded->size() +
			static_cast<unsigned char>((*decoded)[0]);
	}
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK(checksum >= count * payload.size());
	return elapsed;
}

duration_t measure_legacy_gzip_round_trip(size_t count) {
	return measure_legacy_gzip_round_trip_payload(count, 64 * 1024);
}

duration_t measure_legacy_small_gzip_round_trip(size_t count) {
	return measure_legacy_gzip_round_trip_payload(count, 512);
}
#endif

void print_comparison(std::string_view name, size_t operations,
	duration_t legacy, duration_t experimental,
	std::string_view unit = "messages")
{
	const auto legacy_seconds = std::chrono::duration<double>(legacy).count();
	const auto experimental_seconds =
		std::chrono::duration<double>(experimental).count();
	RIWO_TEST_CHECK(legacy_seconds > 0);
	RIWO_TEST_CHECK(experimental_seconds > 0);
	const auto throughput_ratio = legacy_seconds / experimental_seconds;
	const auto throughput_change = (throughput_ratio - 1.0) * 100.0;
	std::cout << std::fixed << std::setprecision(2)
		<< "[PERF] " << name << " comparison: HTTP_EXP is "
		<< throughput_ratio << "x legacy HTTP throughput ("
		<< std::showpos << throughput_change << "%" << std::noshowpos << ", "
		<< operations << ' ' << unit << " each)\n" << std::flush;
}
#endif

void print_policy_comparison(std::string_view name, size_t operations,
	duration_t baseline, duration_t optimized)
{
	const auto baseline_seconds = std::chrono::duration<double>(baseline).count();
	const auto optimized_seconds = std::chrono::duration<double>(optimized).count();
	RIWO_TEST_CHECK(baseline_seconds > 0);
	RIWO_TEST_CHECK(optimized_seconds > 0);
	const auto ratio = baseline_seconds / optimized_seconds;
	std::cout << std::fixed << std::setprecision(2)
		<< "[PERF] " << name << ": optimized policy is " << ratio
		<< "x baseline throughput (" << std::showpos
		<< (ratio - 1.0) * 100.0 << "%" << std::noshowpos << ", "
		<< operations << " requests each)\n" << std::flush;
}

void http_exp_protocol_hot_paths()
{
#if RIWO_HTTP_EXP_COMPARE_LEGACY
	const auto legacy_parse = median_duration(
		protocol_cycle_count, measure_legacy_request_parser);
	const auto legacy_generate = median_duration(
		protocol_cycle_count, measure_legacy_request_generator);
	const auto legacy_response_parse = median_duration(
		protocol_cycle_count, measure_legacy_response_parser);
	const auto legacy_response_generate = median_duration(
		protocol_cycle_count, measure_legacy_response_generator);
	riwo::test::print_performance_result(
		"HTTP legacy/request-head parse (median of 3)", protocol_cycle_count,
		legacy_parse, "message");
	riwo::test::print_performance_result(
		"HTTP legacy/request-head generate (median of 3)", protocol_cycle_count,
		legacy_generate, "message");
	riwo::test::print_performance_result(
		"HTTP legacy/response-head parse (median of 3)", protocol_cycle_count,
		legacy_response_parse, "message");
	riwo::test::print_performance_result(
		"HTTP legacy/response-head generate (median of 3)", protocol_cycle_count,
		legacy_response_generate, "message");
#endif

	const auto experimental_parse = median_duration(
		protocol_cycle_count, measure_http_exp_request_parser);
	const auto experimental_generate = median_duration(
		protocol_cycle_count, measure_http_exp_request_generator);
	const auto experimental_response_parse = median_duration(
		protocol_cycle_count, measure_http_exp_response_parser);
	const auto experimental_response_generate = median_duration(
		protocol_cycle_count, measure_http_exp_response_generator);
	riwo::test::print_performance_result(
		"HTTP_EXP/request-head parse (median of 3)", protocol_cycle_count,
		experimental_parse, "message");
	riwo::test::print_performance_result(
		"HTTP_EXP/request-head generate (median of 3)", protocol_cycle_count,
		experimental_generate, "message");
	riwo::test::print_performance_result(
		"HTTP_EXP/response-head parse (median of 3)", protocol_cycle_count,
		experimental_response_parse, "message");
	riwo::test::print_performance_result(
		"HTTP_EXP/response-head generate (median of 3)", protocol_cycle_count,
		experimental_response_generate, "message");

#if RIWO_HTTP_EXP_COMPARE_LEGACY
	print_comparison("request-head parse", protocol_cycle_count,
		legacy_parse, experimental_parse);
	print_comparison("request-head generate", protocol_cycle_count,
		legacy_generate, experimental_generate);
	print_comparison("response-head parse", protocol_cycle_count,
		legacy_response_parse, experimental_response_parse);
	print_comparison("response-head generate", protocol_cycle_count,
		legacy_response_generate, experimental_response_generate);
#endif
}

#if RIWO_HTTP_EXP_ZLIB_SUPPORT
void http_exp_compression_hot_path()
{
#if RIWO_HTTP_EXP_COMPARE_LEGACY && RIWO_HTTP_ZLIB_SUPPORT
	const auto [legacy, experimental, reused] = balanced_median_durations(
		gzip_cycle_count, measure_legacy_gzip_round_trip,
		measure_http_exp_gzip_round_trip,
		measure_http_exp_gzip_round_trip_reused);
	riwo::test::print_performance_result(
		"HTTP legacy/gzip encode + decode 64 KiB (median of 3)",
		gzip_cycle_count, legacy, "cycle");
#else
	const auto [experimental, reused] = [] {
		const auto one_shot = median_duration(
			gzip_cycle_count, measure_http_exp_gzip_round_trip);
		const auto reused_value = median_duration(
			gzip_cycle_count, measure_http_exp_gzip_round_trip_reused);
		return std::pair {one_shot, reused_value};
	}();
#endif
	riwo::test::print_performance_result(
		"HTTP_EXP/gzip encode + decode 64 KiB (median of 3)",
		gzip_cycle_count, experimental, "cycle");
	riwo::test::print_performance_result(
		"HTTP_EXP/gzip encode + decode 64 KiB, reused codec (median of 3)",
		gzip_cycle_count, reused, "cycle");
	const auto one_shot_seconds =
		std::chrono::duration<double>(experimental).count();
	const auto reused_seconds = std::chrono::duration<double>(reused).count();
	std::cout << std::fixed << std::setprecision(2)
		<< "[PERF] gzip reused codec comparison: HTTP_EXP reused codec is "
		<< one_shot_seconds / reused_seconds << "x one-shot throughput ("
		<< std::showpos
		<< (one_shot_seconds / reused_seconds - 1.0) * 100.0
		<< "%" << std::noshowpos << ")\n" << std::flush;

#if RIWO_HTTP_EXP_COMPARE_LEGACY && RIWO_HTTP_ZLIB_SUPPORT
	print_comparison("gzip encode + decode 64 KiB", gzip_cycle_count,
		legacy, experimental, "cycles");
	const auto [legacy_small, experimental_small, reused_small] =
		balanced_median_durations(small_gzip_cycle_count,
			measure_legacy_small_gzip_round_trip,
			measure_http_exp_small_gzip_round_trip,
			measure_http_exp_small_gzip_round_trip_reused);
	riwo::test::print_performance_result(
		"HTTP legacy/gzip encode + decode 512 B (median of 3)",
		small_gzip_cycle_count, legacy_small, "cycle");
	riwo::test::print_performance_result(
		"HTTP_EXP/gzip encode + decode 512 B (median of 3)",
		small_gzip_cycle_count, experimental_small, "cycle");
	riwo::test::print_performance_result(
		"HTTP_EXP/gzip encode + decode 512 B, reused codec (median of 3)",
		small_gzip_cycle_count, reused_small, "cycle");
	print_comparison("gzip encode + decode 512 B", small_gzip_cycle_count,
		legacy_small, experimental_small, "cycles");
	const auto small_one_shot_seconds =
		std::chrono::duration<double>(experimental_small).count();
	const auto small_reused_seconds =
		std::chrono::duration<double>(reused_small).count();
	std::cout << std::fixed << std::setprecision(2)
		<< "[PERF] gzip reused codec 512 B comparison: HTTP_EXP reused codec is "
		<< small_one_shot_seconds / small_reused_seconds
		<< "x one-shot throughput (" << std::showpos
		<< (small_one_shot_seconds / small_reused_seconds - 1.0) * 100.0
		<< "%" << std::noshowpos << ")\n" << std::flush;
#endif
}

void http_exp_compression_policy()
{
	const auto result = measure_http_exp_compression_policy_loopback();
	riwo::test::print_performance_result(
		"HTTP_EXP/automatic gzip 64 KiB dynamic response",
		secondary_request_count, result.auto_large, "request");
	riwo::test::print_performance_result(
		"HTTP_EXP/cached gzip 64 KiB response",
		secondary_request_count, result.cached_large, "request");
	print_policy_comparison("cached 64 KiB gzip vs dynamic compression",
		secondary_request_count, result.auto_large, result.cached_large);

	riwo::test::print_performance_result(
		"HTTP_EXP/512 B response skipped by minimum-size policy",
		request_count, result.skipped_small, "request");
	riwo::test::print_performance_result(
		"HTTP_EXP/512 B response with forced dynamic gzip",
		request_count, result.forced_small, "request");
	print_policy_comparison("skip 512 B gzip vs forced compression",
		request_count, result.forced_small, result.skipped_small);

	riwo::test::print_performance_result(
		"HTTP_EXP/precompressed MIME 64 KiB skipped",
		secondary_request_count, result.skipped_precompressed, "request");
	riwo::test::print_performance_result(
		"HTTP_EXP/precompressed MIME 64 KiB forced through gzip",
		secondary_request_count, result.forced_precompressed, "request");
	print_policy_comparison("skip already-compressed MIME vs forced gzip",
		secondary_request_count, result.forced_precompressed,
		result.skipped_precompressed);
}
#endif

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"HTTP_EXP loopback throughput", http_exp_loopback_throughput},
		{"HTTP_EXP concurrent loopback throughput",
			http_exp_concurrent_loopback_throughput},
		{"HTTP_EXP protocol hot paths", http_exp_protocol_hot_paths},
#if RIWO_HTTP_EXP_ZLIB_SUPPORT
		{"HTTP_EXP compression hot path", http_exp_compression_hot_path},
		{"HTTP_EXP compression policy", http_exp_compression_policy},
#endif
	});
}
