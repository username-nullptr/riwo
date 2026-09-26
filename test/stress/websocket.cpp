// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/websocket/client.h>
#include <riwo/websocket/server.h>

namespace
{

namespace ws = riwo::websocket;

void low_load_connection_lifecycle_repetition()
{
	const size_t rounds = 64 * RIWO_STRESS_SCALE;
	const auto seed = riwo::test::current_seed();
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	ws::server service(std::move(acceptor), asio::make_strand(context));
	std::atomic_size_t echoed {0};
	service.on_connection("/lifecycle",
	[&](ws::accept_result accepted) -> riwo::awaitable<void>
	{
		for(;;)
		{
			auto read_result = co_await accepted.stream.read<std::string>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, message] = read_result;
			if(error)
				co_return;
			co_await accepted.stream.write_text(message.body,
				riwo::use_awaitable);
			echoed.fetch_add(1, std::memory_order_relaxed);
		}
	});
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	const auto target = std::format("ws://127.0.0.1:{}/lifecycle", port);
	auto completed = asio::co_spawn(asio::make_strand(context),
		[&, target]() -> riwo::awaitable<void>
		{
			auto executor = co_await asio::this_coro::executor;
			for(size_t round = 0; round < rounds; ++round)
			{
				ws::client requester(executor);
				auto stream = co_await requester.open(target, riwo::use_awaitable);
				const auto payload = std::format("lifecycle:{}", round);
				co_await stream.write_text(payload, riwo::use_awaitable);
				auto response = co_await stream.read<std::string>(
					riwo::use_awaitable);
				RIWO_TEST_CHECK_EQ(response.body, payload);
				const auto closed = co_await stream.close(riwo::use_awaitable);
				RIWO_TEST_CHECK(closed.clean);
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
	RIWO_TEST_CHECK_EQ(echoed.load(std::memory_order_relaxed), rounds);
	service.stop();
	context.stop();
	for(auto &runner : runners)
		runner.join();
}

void concurrent_connection_pressure()
{
	constexpr size_t client_count = 16;
	constexpr size_t connection_cycles = 4;
	const size_t messages_per_connection = 16 * RIWO_STRESS_SCALE;
	riwo::io_context_t context;
	asio::ip::tcp::acceptor acceptor(context);
	auto service_executor = asio::make_strand(context);
	ws::server service(std::move(acceptor), service_executor);
	std::atomic_size_t echoed {0};
	service.on_connection("/stress",
	[&](ws::accept_result accepted) -> riwo::awaitable<void>
	{
		for(;;)
		{
			auto read_result = co_await accepted.stream.read<>(
				asio::as_tuple(riwo::use_awaitable));
			auto &[error, message] = read_result;
			if(error)
				co_return;
			co_await accepted.stream.write(message.type,
				riwo::const_buffer(message.body.data(), message.body.size()),
				riwo::use_awaitable);
			echoed.fetch_add(1, std::memory_order_relaxed);
		}
	});
	service.bind({riwo::ip_type::v4, 0}).start();
	const auto port = service.http_server().acceptor_wrap()
		.acceptor().local_endpoint().port();
	const auto target = std::format("ws://127.0.0.1:{}/stress", port);
	std::vector<std::future<void>> futures;
	futures.reserve(client_count);
	for(size_t client_index = 0; client_index < client_count; ++client_index)
	{
		auto client_executor = asio::make_strand(context);
		futures.emplace_back(asio::co_spawn(client_executor,
		[&, target, client_index]() -> riwo::awaitable<void>
		{
			ws::client client(co_await asio::this_coro::executor);
			for(size_t cycle = 0; cycle < connection_cycles; ++cycle)
			{
				auto stream = co_await client.open(target, riwo::use_awaitable);
				for(size_t index = 0; index < messages_per_connection; ++index)
				{
					const bool large_binary = (index % 16) == 0;
					const auto type = large_binary ?
						ws::message_type::binary : ws::message_type::text;
					const auto payload = large_binary ?
						std::string(64 * 1'024,
							static_cast<char>('a' + (client_index + cycle) % 26)) :
						std::format("{}:{}:{}", client_index, cycle, index);
					co_await stream.write(type, asio::buffer(payload),
						riwo::use_awaitable);
					auto response = co_await stream.read<>(riwo::use_awaitable);
					RIWO_TEST_CHECK_EQ(response.type, type);
					RIWO_TEST_CHECK_EQ(
						std::string(reinterpret_cast<const char*>(response.body.data()),
							response.body.size()), payload);
				}
				const auto closed = co_await stream.close(riwo::use_awaitable);
				RIWO_TEST_CHECK(closed.clean);
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
	RIWO_TEST_CHECK_EQ(echoed.load(),
		client_count * connection_cycles * messages_per_connection);
	service.stop();
	context.stop();
	for(auto &runner : runners)
		runner.join();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"low-load WebSocket connection lifecycle repetition",
			low_load_connection_lifecycle_repetition},
		{"concurrent WebSocket connection pressure", concurrent_connection_pressure},
	});
}
