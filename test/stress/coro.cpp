// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/coro/mutex.h>
#include <riwo/coro/shared_mutex.h>

namespace
{

void low_load_mutex_lifecycle_repetition()
{
	constexpr size_t worker_count = 2;
	constexpr size_t iterations = 4;
	const size_t rounds = 64 * RIWO_STRESS_SCALE;
	const auto seed = riwo::test::current_seed();
	for(size_t round = 0; round < rounds; ++round)
	{
		riwo::io_context_t context;
		riwo::coro::mutex mutex;
		std::atomic_size_t active {0};
		std::atomic_bool overlap {false};
		std::atomic_size_t counter {0};
		std::array<std::future<void>,worker_count> futures;
		for(size_t worker = 0; worker < worker_count; ++worker)
		{
			futures[worker] = asio::co_spawn(context,
			[&, worker]() -> riwo::awaitable<void>
			{
				for(size_t index = 0; index < iterations; ++index)
				{
					co_await mutex.lock();
					if(active.fetch_add(1, std::memory_order_relaxed) != 0)
						overlap.store(true, std::memory_order_relaxed);
					counter.fetch_add(1, std::memory_order_relaxed);
					if(((seed ^ round ^ worker ^ index) & 1U) != 0)
						co_await asio::post(riwo::use_awaitable);
					if(active.fetch_sub(1, std::memory_order_relaxed) != 1)
						overlap.store(true, std::memory_order_relaxed);
					mutex.unlock();
					co_await asio::post(riwo::use_awaitable);
				}
			}, riwo::use_future);
		}

		std::array<std::thread,2> runners;
		for(auto &runner : runners)
			runner = std::thread([&] { context.run(); });
		for(auto &future : futures)
			future.get();
		for(auto &runner : runners)
			runner.join();
		RIWO_TEST_CHECK_EQ(counter.load(std::memory_order_relaxed),
			worker_count * iterations);
		RIWO_TEST_CHECK_EQ(active.load(std::memory_order_relaxed), 0U);
		RIWO_TEST_CHECK(not overlap.load(std::memory_order_relaxed));
		RIWO_TEST_CHECK(not mutex.is_locked());
	}
}

void coroutine_mutex_pressure()
{
	constexpr size_t worker_count = 128;
	const size_t iterations = 1'000 * RIWO_STRESS_SCALE;
	riwo::io_context_t context;
	riwo::coro::mutex mutex;
	std::atomic_uint64_t counter {0};
	std::atomic_size_t active {0};
	std::atomic_bool overlap {false};
	std::vector<std::future<void>> futures;
	futures.reserve(worker_count);

	for(size_t worker = 0; worker < worker_count; ++worker)
	{
		futures.emplace_back(asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			for(size_t index = 0; index < iterations; ++index)
			{
				co_await mutex.lock();
				if(active.fetch_add(1) != 0)
					overlap.store(true, std::memory_order_relaxed);
				counter.fetch_add(1, std::memory_order_relaxed);
				if(active.fetch_sub(1) != 1)
					overlap.store(true, std::memory_order_relaxed);
				mutex.unlock();
				if((index & 31U) == 0)
					co_await asio::post(riwo::use_awaitable);
			}
		}, riwo::use_future));
	}

	std::array<std::thread,4> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	for(auto &future : futures)
		future.get();
	for(auto &runner : runners)
		runner.join();

	RIWO_TEST_CHECK_EQ(counter.load(std::memory_order_relaxed),
		worker_count * iterations);
	RIWO_TEST_CHECK_EQ(active.load(), 0U);
	RIWO_TEST_CHECK(not overlap.load());
	RIWO_TEST_CHECK(not mutex.is_locked());
}

void shared_mutex_reader_writer_pressure()
{
	constexpr size_t writer_count = 16;
	constexpr size_t reader_count = 64;
	const size_t iterations = 500 * RIWO_STRESS_SCALE;
	riwo::io_context_t context;
	riwo::coro::shared_mutex mutex;
	std::atomic_uint64_t value {0};
	std::atomic_size_t active_readers {0};
	std::atomic_size_t active_writers {0};
	std::atomic_bool concurrent_writers {false};
	std::atomic_bool writer_reader_overlap {false};
	std::atomic_bool reader_writer_overlap {false};
	std::vector<std::future<void>> futures;
	futures.reserve(writer_count + reader_count);

	for(size_t writer = 0; writer < writer_count; ++writer)
	{
		futures.emplace_back(asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			for(size_t index = 0; index < iterations; ++index)
			{
				co_await mutex.lock();
				if(active_writers.fetch_add(1) != 0)
					concurrent_writers.store(true, std::memory_order_relaxed);
				if(active_readers.load() != 0)
					writer_reader_overlap.store(true, std::memory_order_relaxed);
				value.fetch_add(1, std::memory_order_relaxed);
				if(active_writers.fetch_sub(1) != 1)
					concurrent_writers.store(true, std::memory_order_relaxed);
				mutex.unlock();
				if((index & 15U) == 0)
					co_await asio::post(riwo::use_awaitable);
			}
		}, riwo::use_future));
	}
	for(size_t reader = 0; reader < reader_count; ++reader)
	{
		futures.emplace_back(asio::co_spawn(context,
		[&]() -> riwo::awaitable<void>
		{
			std::uint64_t previous = 0;
			for(size_t index = 0; index < iterations; ++index)
			{
				co_await mutex.lock_shared();
				active_readers.fetch_add(1);
				if(active_writers.load() != 0)
					reader_writer_overlap.store(true, std::memory_order_relaxed);
				const auto observed = value.load(std::memory_order_relaxed);
				active_readers.fetch_sub(1);
				mutex.unlock_shared();
				RIWO_TEST_CHECK(observed >= previous);
				previous = observed;
				if((index & 31U) == 0)
					co_await asio::post(riwo::use_awaitable);
			}
		}, riwo::use_future));
	}

	std::array<std::thread,4> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	for(auto &future : futures)
		future.get();
	for(auto &runner : runners)
		runner.join();

	RIWO_TEST_CHECK_EQ(value.load(), writer_count * iterations);
	RIWO_TEST_CHECK_EQ(active_readers.load(), 0U);
	RIWO_TEST_CHECK_EQ(active_writers.load(), 0U);
	RIWO_TEST_CHECK(not concurrent_writers.load());
	RIWO_TEST_CHECK(not writer_reader_overlap.load());
	RIWO_TEST_CHECK(not reader_writer_overlap.load());
	RIWO_TEST_CHECK(not mutex.is_locked());
}

void timed_mutex_waiter_pressure()
{
	using namespace std::chrono_literals;
	constexpr size_t worker_count = 64;
	const size_t iterations = 250 * RIWO_STRESS_SCALE;
	riwo::io_context_t context;
	riwo::coro::mutex mutex;
	std::atomic_size_t active {0};
	std::atomic_size_t acquired {0};
	std::atomic_size_t timed_out {0};
	std::atomic_bool overlap {false};
	std::vector<std::future<void>> futures;
	futures.reserve(worker_count);

	for(size_t worker = 0; worker < worker_count; ++worker)
	{
		futures.emplace_back(asio::co_spawn(context,
		[&, worker]() -> riwo::awaitable<void>
		{
			for(size_t index = 0; index < iterations; ++index)
			{
				const auto timeout = ((worker + index) & 3U) == 0 ? 0us : 200us;
				if(co_await mutex.try_lock_for(timeout))
				{
					if(active.fetch_add(1, std::memory_order_relaxed) != 0)
						overlap.store(true, std::memory_order_relaxed);
					acquired.fetch_add(1, std::memory_order_relaxed);
					co_await asio::post(riwo::use_awaitable);
					if(active.fetch_sub(1, std::memory_order_relaxed) != 1)
						overlap.store(true, std::memory_order_relaxed);
					mutex.unlock();
				}
				else
					timed_out.fetch_add(1, std::memory_order_relaxed);
				if((index & 7U) == 0)
					co_await asio::post(riwo::use_awaitable);
			}
		}, riwo::use_future));
	}

	std::array<std::thread,4> runners;
	for(auto &runner : runners)
		runner = std::thread([&] { context.run(); });
	for(auto &future : futures)
		future.get();
	for(auto &runner : runners)
		runner.join();

	RIWO_TEST_CHECK_EQ(acquired.load() + timed_out.load(),
		worker_count * iterations);
	RIWO_TEST_CHECK(acquired.load() != 0);
	RIWO_TEST_CHECK(timed_out.load() != 0);
	RIWO_TEST_CHECK_EQ(active.load(), 0U);
	RIWO_TEST_CHECK(not overlap.load());
	RIWO_TEST_CHECK(not mutex.is_locked());
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"low-load mutex lifecycle repetition", low_load_mutex_lifecycle_repetition},
		{"coroutine mutex pressure", coroutine_mutex_pressure},
		{"shared mutex reader/writer pressure", shared_mutex_reader_writer_pressure},
		{"timed mutex waiter pressure", timed_mutex_waiter_pressure},
	});
}
