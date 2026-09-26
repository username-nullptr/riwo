// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/core/async_expected.h>

namespace
{

using namespace std::chrono_literals;

void dispatch_and_post_ordering()
{
	riwo::io_context_t context;
	std::vector<int> order;

	asio::post(context, [&]
	{
		order.push_back(1);
		riwo::dispatch(context, [&] { order.push_back(2); });
		riwo::post(context, [&]() -> riwo::awaitable<void>
		{
			order.push_back(4);
			co_return ;
		}, [&order](std::exception_ptr error)
		{
			RIWO_TEST_CHECK(not error);
			order.push_back(5);
		});
		order.push_back(3);
	});

	context.run();
	RIWO_TEST_CHECK_EQ(order, (std::vector<int>{1, 2, 3, 4, 5}));
}

void synchronous_dispatch_context_selection()
{
	riwo::io_context_t context;
	std::vector<int> order;

	asio::post(context, [&]
	{
		const auto caller = std::this_thread::get_id();
		order.push_back(1);
		const auto worker = riwo::dispatch(context, [&]
		{
			order.push_back(2);
			return std::this_thread::get_id();
		}, riwo::use_sync);
		order.push_back(3);
		RIWO_TEST_CHECK(worker == caller);
	});

	context.run();
	RIWO_TEST_CHECK_EQ(order, (std::vector<int>{1, 2, 3}));

	context.restart();
	auto work_guard = asio::make_work_guard(context);
	std::promise<void> runner_ready;
	auto ready = runner_ready.get_future();
	auto runner = std::async(std::launch::async, [&]
	{
		runner_ready.set_value();
		context.run();
	});
	ready.get();

	const auto caller = std::this_thread::get_id();
	const auto worker = riwo::dispatch(context,
		[] { return std::this_thread::get_id(); }, riwo::use_sync);
	RIWO_TEST_CHECK(worker != caller);

	work_guard.reset();
	runner.get();
}

void queued_and_delayed_work()
{
	riwo::io_context_t context;
	std::vector<int> order;

	auto value = riwo::post(context, [&]
	{
		order.push_back(1);
		return 42;
	}, riwo::use_future);
	riwo::post(context, [&] { order.push_back(2); });
	riwo::post(context, 1ms, [&] { order.push_back(3); });

	context.run();
	RIWO_TEST_CHECK_EQ(value.get(), 42);
	RIWO_TEST_CHECK_EQ(order.size(), 3U);
	RIWO_TEST_CHECK_EQ(order[0], 1);
	RIWO_TEST_CHECK_EQ(order[1], 2);
	RIWO_TEST_CHECK_EQ(order[2], 3);
}

void future_exception_propagation()
{
	riwo::io_context_t context;
	auto result = riwo::post(context, []() -> int
	{
		throw std::runtime_error("execution probe");
	}, riwo::use_future);

	context.run();
	RIWO_TEST_CHECK_THROWS(result.get(), std::runtime_error);
}

void delayed_work_cancellation()
{
	riwo::io_context_t context;
	bool invoked = false;
	auto cancel = riwo::post(context, 1ms, [&] { invoked = true; });
	cancel();
	context.run();
	RIWO_TEST_CHECK(not invoked);
}

void cross_thread_delayed_cancellation()
{
	riwo::io_context_t context;
	std::atomic_bool invoked = false;
	auto cancel = riwo::post(context, 50ms, [&] { invoked = true; });
	auto runner = std::async(std::launch::async, [&] { context.run(); });

	cancel();
	runner.get();
	RIWO_TEST_CHECK(not invoked.load());
}

void local_dispatch_and_sleep()
{
	riwo::io_context_t context;
	const auto [result, work_count] = riwo::local_dispatch(
		context, [] { return 7 * 6; }, riwo::use_sync
	);
	RIWO_TEST_CHECK_EQ(result, 42);
	RIWO_TEST_CHECK_EQ(*work_count, 1U);

	const auto before = std::chrono::steady_clock::now();
	riwo::sleep_for(1ms);
	RIWO_TEST_CHECK(std::chrono::steady_clock::now() >= before);
}

void local_event_pumps()
{
	riwo::io_context_t stopped_context;
	stopped_context.run();
	auto [value, work_count] = riwo::local_dispatch(stopped_context,
		[]() -> riwo::awaitable<int>
		{
			co_return 42;
		}, riwo::use_sync);
	RIWO_TEST_CHECK_EQ(value, 42);
	RIWO_TEST_CHECK(*work_count > 0);

	asio::thread_pool pool(1);
	auto future = riwo::local_dispatch(pool, [] { return 21 * 2; }, riwo::use_future);
	auto [pool_value, pool_work_count] = future.get();
	pool.join();
	RIWO_TEST_CHECK_EQ(pool_value, 42);
	RIWO_TEST_CHECK_EQ(*pool_work_count, 0U);

	RIWO_TEST_CHECK_THROWS(riwo::local_dispatch(stopped_context,
		[]() -> riwo::awaitable<void>
		{
			throw std::runtime_error("local dispatch probe");
			co_return ;
		}, riwo::use_sync), std::runtime_error);
}

void asynchronous_sleep_tokens()
{
	riwo::io_context_t context;
	auto future = riwo::sleep_for(context, 1ms, riwo::use_future);
	bool callback_called = false;
	riwo::sleep_until(context, std::chrono::steady_clock::now(),
		[&](const riwo::error_code &error)
		{
			RIWO_TEST_CHECK(not error);
			callback_called = true;
		});

	context.run();
	future.get();
	RIWO_TEST_CHECK(callback_called);
}

void async_work_never_completes_inline()
{
	riwo::io_context_t context;
	bool initiating = true;
	bool completed = false;

	riwo::async_work<>::handle(context,
		[](auto handler) mutable { std::move(handler)(); },
		[&]
		{
			RIWO_TEST_CHECK(not initiating);
			completed = true;
		});
	initiating = false;

	RIWO_TEST_CHECK(not completed);
	context.run();
	RIWO_TEST_CHECK(completed);
}

void posted_completion_uses_immediate_executor()
{
	riwo::io_context_t io_context;
	riwo::io_context_t completion_context;
	bool initiating = true;
	bool completed = false;

	riwo::post_completion(io_context.get_executor(),
		asio::bind_immediate_executor(completion_context.get_executor(),
		[&](riwo::error_code error, int value)
		{
			RIWO_TEST_CHECK(not initiating);
			RIWO_TEST_CHECK(not error);
			RIWO_TEST_CHECK_EQ(value, 42);
			completed = true;
		}), riwo::error_code {}, 42);
	initiating = false;

	RIWO_TEST_CHECK(not completed);
	completion_context.run();
	RIWO_TEST_CHECK(completed);
}

void periodic_timer()
{
	riwo::io_context_t context;
	int ticks = 0;
	riwo::work_canceller_t cancel;
	cancel = riwo::start_timer(context, 1ms, [&]
	{
		if(++ticks == 3)
			cancel();
	});
	context.run();
	RIWO_TEST_CHECK_EQ(ticks, 3);
}

void cross_thread_periodic_cancellation()
{
	riwo::io_context_t context;
	std::atomic_size_t ticks = 0;
	auto cancel = riwo::start_timer(context, 1ms, [&]
	{
		ticks.fetch_add(1, std::memory_order_relaxed);
	});
	auto runner = std::async(std::launch::async, [&] { context.run(); });

	for(size_t retry = 0; retry < 1'000 and ticks.load() == 0; ++retry)
		std::this_thread::sleep_for(1ms);
	cancel();
	runner.get();
	RIWO_TEST_CHECK(ticks.load() > 0);
}

void awaitable_and_absolute_work()
{
	riwo::io_context_t context;
	auto awaitable_result = riwo::post(context,
		[]() -> riwo::awaitable<int> { co_return 42; }, riwo::use_future);
	bool absolute_invoked = false;
	riwo::post(context, std::chrono::steady_clock::now(),
		[&] { absolute_invoked = true; });

	int ticks = 0;
	riwo::work_canceller_t cancel;
	cancel = riwo::start_timer(context, 1ms,
		[&](const riwo::work_canceller_t &stop)
		{
			++ticks;
			stop();
		}, true);
	context.run();
	RIWO_TEST_CHECK_EQ(awaitable_result.get(), 42);
	RIWO_TEST_CHECK(absolute_invoked);
	RIWO_TEST_CHECK_EQ(ticks, 1);
}

void runtime_recovers_from_handler_exception()
{
	riwo::post([] { throw std::runtime_error("runtime probe"); });
	RIWO_TEST_CHECK_THROWS(riwo::exec(), std::runtime_error);
	RIWO_TEST_CHECK(not riwo::is_run());
}

void global_event_loop()
{
	auto result = std::async(std::launch::async, [] { return riwo::exec(); });
	for(size_t retry = 0; retry < 1'000 and not riwo::is_run(); ++retry)
		std::this_thread::sleep_for(1ms);
	const bool started = riwo::is_run();
	RIWO_TEST_CHECK_THROWS(riwo::exec(), riwo::runtime_error);
	riwo::post([] { riwo::exit(7); });
	RIWO_TEST_CHECK_EQ(result.get(), 7);
	RIWO_TEST_CHECK(started);
	RIWO_TEST_CHECK(not riwo::is_run());
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"dispatch and post ordering", dispatch_and_post_ordering},
		{"synchronous dispatch context selection", synchronous_dispatch_context_selection},
		{"queued and delayed work", queued_and_delayed_work},
		{"future exception propagation", future_exception_propagation},
		{"delayed work cancellation", delayed_work_cancellation},
		{"cross-thread delayed cancellation", cross_thread_delayed_cancellation},
		{"local dispatch and sleep", local_dispatch_and_sleep},
		{"local event pumps", local_event_pumps},
		{"asynchronous sleep tokens", asynchronous_sleep_tokens},
		{"async work never completes inline", async_work_never_completes_inline},
		{"posted completion uses immediate executor",
			posted_completion_uses_immediate_executor},
		{"periodic timer", periodic_timer},
		{"cross-thread periodic cancellation", cross_thread_periodic_cancellation},
		{"awaitable and absolute work", awaitable_and_absolute_work},
		{"runtime recovers from handler exception", runtime_recovers_from_handler_exception},
		{"global event loop", global_event_loop},
	});
}
