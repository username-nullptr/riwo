// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/http/client.h>
#include <riwo/http/server.h>

namespace
{

#ifdef NDEBUG
constexpr size_t warmup_count = 100;
constexpr size_t request_count = 2'000 * riwo::test::performance_scale;
#else
constexpr size_t warmup_count = 10;
constexpr size_t request_count = 200 * riwo::test::performance_scale;
#endif
constexpr size_t secondary_request_count = request_count / 10;

void http_loopback_throughput()
{
	using namespace riwo::http;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	server service(std::move(acceptor));
	const std::string large_body(64 * 1024, 'x');
	auto server_config = service.config();
	server_config.keepalive_time = std::chrono::seconds(30);
	service.set_config(server_config);
	service
		.bind({riwo::ip_type::v4, 0})
		.on_request<method::get>("/benchmark",
		[](server::context_t &request_context) -> riwo::awaitable<void>
		{
			constexpr std::string_view body = "ok";
			co_await request_context.response().write(
				asio::buffer(body), riwo::use_awaitable
			);
		})
		.on_request<method::get>("/benchmark-large",
		[&large_body](server::context_t &request_context) -> riwo::awaitable<void>
		{
			co_await request_context.response().write(
				asio::buffer(large_body), riwo::use_awaitable
			);
		})
		.start();

	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	RIWO_TEST_CHECK(port != 0);
	const auto url = std::format("http://127.0.0.1:{}/benchmark", port);
	const auto large_url = std::format(
		"http://127.0.0.1:{}/benchmark-large", port);
	client requester(context.get_executor());
	auto completed = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		try
		{
			auto measure = [&](const std::string &target, std::string_view expected_body,
				size_t iterations, bool close) -> riwo::awaitable<std::chrono::steady_clock::duration>
			{
				const auto begin = std::chrono::steady_clock::now();
				for(size_t index = 0; index < iterations; ++index)
				{
					client::req_info request_info(target);
					if( close )
						request_info.arg.set_header(header::connection, "close");
					auto request = co_await requester.request_get(
						std::move(request_info), riwo::use_awaitable);
					RIWO_TEST_CHECK(request);
					RIWO_TEST_CHECK_EQ(
						co_await request->wait_reply(riwo::use_awaitable), status::ok
					);
					RIWO_TEST_CHECK_EQ(
						co_await request->reply()->read<std::string>(riwo::use_awaitable),
						expected_body
					);
				}
				co_return std::chrono::steady_clock::now() - begin;
			};
			auto report = [&](std::string_view name, const std::string &target,
				std::string_view body, size_t iterations, bool close)
				-> riwo::awaitable<void>
			{
				co_await measure(target, body,
					std::min(warmup_count, iterations), close);
				std::array<std::chrono::steady_clock::duration,3> samples {};
				for(auto &sample : samples)
					sample = co_await measure(target, body, iterations, close);
				std::ranges::sort(samples);
				riwo::test::print_performance_result(
					name, iterations, samples[1], "request");
			};

			co_await report("HTTP/1.1 keep-alive, 2 B body (median of 3)",
				url, "ok", request_count, false);
			co_await report("HTTP/1.1 keep-alive, 64 KiB body (median of 3)",
				large_url, large_body, secondary_request_count, false);
			co_await report("HTTP/1.1 reconnect, 2 B body (median of 3)",
				url, "ok", secondary_request_count, true);
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

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"HTTP loopback throughput", http_loopback_throughput},
	});
}
