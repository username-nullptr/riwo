// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/http/client.h>
#include <riwo/http/server.h>

namespace
{

void low_load_client_lifecycle_repetition()
{
	using namespace riwo::http;
	const size_t rounds = 200 * RIWO_STRESS_SCALE;
	const auto seed = riwo::test::current_seed();
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	server service(std::move(acceptor), asio::make_strand(context));
	std::atomic_size_t handled {0};
	service.bind({riwo::ip_type::v4, 0})
		.on_request<method::get>("/lifecycle",
		[&](server::context_t &request_context) -> riwo::awaitable<void>
		{
			const auto body = request_context.request().parameter("round")
				.value_or("missing").to_string();
			handled.fetch_add(1, std::memory_order_relaxed);
			co_await request_context.response().write(
				asio::buffer(body), riwo::use_awaitable);
		})
		.start();
	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	const auto target = std::format(
		"http://127.0.0.1:{}/lifecycle?round=", port);
	auto completed = asio::co_spawn(asio::make_strand(context),
		[&, target]() -> riwo::awaitable<void>
		{
			auto executor = co_await asio::this_coro::executor;
			for(size_t round = 0; round < rounds; ++round)
			{
				client requester(executor);
				auto request = co_await requester.request_get(
					target + std::to_string(round), riwo::use_awaitable);
				RIWO_TEST_CHECK(request);
				RIWO_TEST_CHECK_EQ(
					co_await request->wait_reply(riwo::use_awaitable), status::ok);
				RIWO_TEST_CHECK_EQ(
					co_await request->reply()->read<std::string>(riwo::use_awaitable),
					std::to_string(round));
				if(((seed ^ round) & 3U) == 0)
					co_await asio::post(riwo::use_awaitable);
			}
		}, riwo::use_future);

	std::array<std::thread,2> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	try
	{
		completed.get();
	}
	catch(...)
	{
		service.stop();
		context.stop();
		for(auto &runner : runners)
			runner.join();
		throw;
	}
	RIWO_TEST_CHECK_EQ(handled.load(std::memory_order_relaxed), rounds);
	service.stop();
	context.stop();
	for(auto &runner : runners)
		runner.join();
}

void concurrent_keep_alive_pressure()
{
	using namespace riwo::http;
	constexpr size_t client_count = 12;
	const size_t requests_per_client = 100 * RIWO_STRESS_SCALE;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	auto service_executor = asio::make_strand(context);
	server service(std::move(acceptor), service_executor);
	std::atomic_size_t handled {0};
	service.bind({riwo::ip_type::v4, 0})
		.on_request<method::get>("/stress",
		[&](server::context_t &request_context) -> riwo::awaitable<void>
		{
			const auto requested_size = request_context.request().parameter("size")
				.value_or("0").to_uint().value_or(0);
			const auto fill_value = request_context.request().parameter("fill")
				.value_or("0").to_uint().value_or(0);
			const auto body_size = std::min<size_t>(requested_size, 64 * 1'024);
			const std::string body(body_size,
				static_cast<char>('a' + fill_value % 26));
			handled.fetch_add(1, std::memory_order_relaxed);
			co_await request_context.response().write(
				asio::buffer(body), riwo::use_awaitable);
		})
		.start();
	const auto port = service.acceptor_wrap().acceptor().local_endpoint().port();
	const auto target = std::format("http://127.0.0.1:{}/stress", port);
	std::vector<std::future<void>> futures;
	futures.reserve(client_count);
	for(size_t index = 0; index < client_count; ++index)
	{
		auto client_executor = asio::make_strand(context);
		futures.emplace_back(asio::co_spawn(client_executor,
		[&, target, index]() -> riwo::awaitable<void>
		{
			auto executor = co_await asio::this_coro::executor;
			client requester(executor);
			for(size_t request_index = 0; request_index < requests_per_client;
				++request_index)
			{
				const auto body_size = (request_index % 32) == 0 ?
					size_t {64 * 1'024} : size_t {32 + request_index % 224};
				const auto fill_value = (index * 17 + request_index) % 26;
				const auto request_target = std::format(
					"{}?size={}&fill={}", target, body_size, fill_value);
				std::unique_ptr<client> transient;
				client *active_client = &requester;
				if((request_index % 10) == 0)
				{
					transient = std::make_unique<client>(executor);
					active_client = transient.get();
				}
				auto request = co_await active_client->request_get(
					request_target, riwo::use_awaitable);
				RIWO_TEST_CHECK(request);
				RIWO_TEST_CHECK_EQ(
					co_await request->wait_reply(riwo::use_awaitable), status::ok);
				auto body = co_await request->reply()->read<std::string>(
					riwo::use_awaitable);
				RIWO_TEST_CHECK_EQ(body.size(), body_size);
				RIWO_TEST_CHECK(std::ranges::all_of(body,
					[expected = static_cast<char>('a' + fill_value)](char value) {
						return value == expected;
					}));
			}
		}, riwo::use_future));
	}

	std::array<std::thread,4> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	try
	{
		for(auto &future : futures)
			future.get();
	}
	catch(...)
	{
		service.stop();
		context.stop();
		for(auto &runner : runners)
			runner.join();
		throw;
	}
	RIWO_TEST_CHECK_EQ(handled.load(), client_count * requests_per_client);
	service.stop();
	context.stop();
	for(auto &runner : runners)
		runner.join();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"low-load HTTP client lifecycle repetition",
			low_load_client_lifecycle_repetition},
		{"concurrent HTTP keep-alive pressure", concurrent_keep_alive_pressure},
	});
}
