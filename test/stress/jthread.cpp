// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/core/jthread.h>
#include <array>
#include <functional>
#include <memory>

namespace
{

static_assert(RIWO_HAS_STD_JTHREAD == 0);
constexpr size_t scale = RIWO_STRESS_SCALE;

void repeated_jthread_lifecycle()
{
	const size_t rounds = 250 * scale;
	std::atomic_size_t started {0};
	std::atomic_size_t stopped {0};
	for(size_t round = 0; round < rounds; ++round)
	{
		riwo::jthread worker([&](riwo::stop_token token)
		{
			started.fetch_add(1, std::memory_order_release);
			while( not token.stop_requested() )
				std::this_thread::yield();
			stopped.fetch_add(1, std::memory_order_release);
		});
		while( started.load(std::memory_order_acquire) != round + 1 )
			std::this_thread::yield();
	}
	RIWO_TEST_CHECK_EQ(started.load(std::memory_order_acquire), rounds);
	RIWO_TEST_CHECK_EQ(stopped.load(std::memory_order_acquire), rounds);
}

void concurrent_stop_requests()
{
	constexpr size_t requester_count = 8;
	constexpr size_t callback_count = 16;
	const size_t rounds = 100 * scale;
	for(size_t round = 0; round < rounds; ++round)
	{
		riwo::stop_source source;
		std::array<std::atomic_uint8_t,callback_count> callback_hits {};
		using callback_t = riwo::stop_callback<std::function<void()>>;
		std::array<std::unique_ptr<callback_t>,callback_count> callbacks;
		for(size_t index = 0; index < callback_count; ++index)
		{
			callbacks[index] = std::make_unique<callback_t>(
				source.get_token(), [&, index]
				{
					callback_hits[index].fetch_add(1, std::memory_order_relaxed);
				}
			);
		}

		std::atomic_size_t ready {0};
		std::atomic_bool start {false};
		std::atomic_size_t successful_requests {0};
		std::array<std::thread,requester_count> requesters;
		for(auto &requester : requesters)
		{
			requester = std::thread([&]
			{
				ready.fetch_add(1, std::memory_order_release);
				while( not start.load(std::memory_order_acquire) )
					std::this_thread::yield();
				if( source.request_stop() )
					successful_requests.fetch_add(1, std::memory_order_relaxed);
			});
		}
		while( ready.load(std::memory_order_acquire) != requester_count )
			std::this_thread::yield();
		start.store(true, std::memory_order_release);
		for(auto &requester : requesters)
			requester.join();

		RIWO_TEST_CHECK_EQ(successful_requests.load(), 1U);
		for(const auto &hits : callback_hits)
			RIWO_TEST_CHECK_EQ(hits.load(std::memory_order_relaxed), uint8_t {1});
	}
}

void callback_removal_races_stop()
{
	constexpr size_t callback_count = 32;
	constexpr size_t remover_count = 4;
	const size_t rounds = 100 * scale;
	for(size_t round = 0; round < rounds; ++round)
	{
		riwo::stop_source source;
		std::array<std::atomic_uint8_t,callback_count> callback_hits {};
		using callback_t = riwo::stop_callback<std::function<void()>>;
		std::array<std::unique_ptr<callback_t>,callback_count> callbacks;
		for(size_t index = 0; index < callback_count; ++index)
		{
			callbacks[index] = std::make_unique<callback_t>(
				source.get_token(), [&, index]
				{
					callback_hits[index].fetch_add(1, std::memory_order_relaxed);
					if( ((index + round) & 3U) == 0 )
						std::this_thread::yield();
				}
			);
		}

		std::atomic_size_t ready {0};
		std::atomic_bool start {false};
		std::array<std::thread,remover_count> removers;
		for(size_t remover = 0; remover < remover_count; ++remover)
		{
			removers[remover] = std::thread([&, remover]
			{
				ready.fetch_add(1, std::memory_order_release);
				while( not start.load(std::memory_order_acquire) )
					std::this_thread::yield();
				for(size_t index = remover; index < callback_count;
					index += remover_count)
				{
					callbacks[index].reset();
					if( ((index + round) & 1U) == 0 )
						std::this_thread::yield();
				}
			});
		}
		std::thread requester([&]
		{
			ready.fetch_add(1, std::memory_order_release);
			while( not start.load(std::memory_order_acquire) )
				std::this_thread::yield();
			source.request_stop();
		});

		while( ready.load(std::memory_order_acquire) != remover_count + 1 )
			std::this_thread::yield();
		start.store(true, std::memory_order_release);
		for(auto &remover : removers)
			remover.join();
		requester.join();

		for(const auto &hits : callback_hits)
			RIWO_TEST_CHECK(hits.load(std::memory_order_relaxed) <= 1);
	}
}

void many_polling_workers()
{
	constexpr size_t worker_count = 16;
	const size_t rounds = 50 * scale;
	for(size_t round = 0; round < rounds; ++round)
	{
		std::atomic_size_t ready {0};
		std::atomic_size_t stopped {0};
		std::array<riwo::jthread,worker_count> workers;
		for(auto &worker : workers)
		{
			worker = riwo::jthread([&](riwo::stop_token token)
			{
				ready.fetch_add(1, std::memory_order_release);
				while( not token.stop_requested() )
					std::this_thread::yield();
				stopped.fetch_add(1, std::memory_order_release);
			});
		}
		while( ready.load(std::memory_order_acquire) != worker_count )
			std::this_thread::yield();
		for(auto &worker : workers)
			RIWO_TEST_CHECK(worker.request_stop());
		for(auto &worker : workers)
			worker.join();
		RIWO_TEST_CHECK_EQ(stopped.load(std::memory_order_acquire), worker_count);
	}
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"repeated jthread lifecycle", repeated_jthread_lifecycle},
		{"concurrent stop requests", concurrent_stop_requests},
		{"callback removal races stop", callback_removal_races_stop},
		{"many polling workers", many_polling_workers},
	});
}
