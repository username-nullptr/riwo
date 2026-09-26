// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/core/atomic_mutex.h>
#include <riwo/core/shared_mutex.h>

#include <algorithm>
#include <array>
#include <barrier>
#include <cstdlib>
#include <format>
#include <iostream>
#include <limits>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

#ifdef NDEBUG
constexpr size_t iterations = 100'000 * riwo::test::performance_scale;
#else
constexpr size_t iterations = 10'000 * riwo::test::performance_scale;
#endif

constexpr size_t sample_count = 3;

template <typename Func>
void measure_and_print(
	std::string name, size_t operations, Func &&func, std::string_view unit
)
{
	std::cout << "[SAMPLE] " << name << ": warmup" << std::endl;
	func();
	std::array<std::chrono::steady_clock::duration,sample_count> samples {};
	for(size_t index = 0; index < samples.size(); ++index)
	{
		std::cout << "[SAMPLE] " << name << ": " << index + 1 << '/'
			<< samples.size() << std::endl;
		samples[index] = func();
	}
	std::ranges::sort(samples);
	riwo::test::print_performance_result(
		name, operations, samples[sample_count / 2], unit
	);
}

template <typename Mutex, bool Shared = false>
std::chrono::steady_clock::duration measure_uncontended()
{
	Mutex mutex;
	const auto begin = std::chrono::steady_clock::now();
	for(size_t index = 0; index < iterations; ++index)
	{
		if constexpr( Shared )
		{
			mutex.lock_shared();
			mutex.unlock_shared();
		}
		else
		{
			mutex.lock();
			mutex.unlock();
		}
	}
	return std::chrono::steady_clock::now() - begin;
}

template <typename Mutex>
std::chrono::steady_clock::duration measure_exclusive(
	size_t thread_count, size_t work
)
{
	Mutex mutex;
	size_t value = 0;
	std::barrier start(static_cast<std::ptrdiff_t>(thread_count + 1));
	std::vector<std::thread> workers;
	workers.reserve(thread_count);
	for(size_t thread = 0; thread < thread_count; ++thread)
	{
		workers.emplace_back([&]
		{
			start.arrive_and_wait();
			for(size_t index = 0; index < iterations; ++index)
			{
				std::lock_guard lock(mutex);
				++value;
				for(size_t count = 0; count < work; ++count)
					riwo::none_instruction();
			}
		});
	}
	const auto begin = std::chrono::steady_clock::now();
	start.arrive_and_wait();
	for(auto &worker : workers)
		worker.join();
	const auto elapsed = std::chrono::steady_clock::now() - begin;
	RIWO_TEST_CHECK_EQ(value, thread_count * iterations);
	return elapsed;
}

template <typename Mutex>
std::chrono::steady_clock::duration measure_shared(
	size_t thread_count, size_t write_interval
)
{
	// std::shared_mutex does not guarantee writer fairness.  Periodic rendezvous
	// bound ordinary writer starvation while leaving the measured lock mix
	// unchanged between checkpoints.  They cannot recover from a platform
	// runtime deadlock inside lock/unlock; such a baseline is skipped below.
	constexpr size_t checkpoint_interval = 2'048;
	Mutex mutex;
	size_t value = 0;
	std::barrier start(static_cast<std::ptrdiff_t>(thread_count + 1));
	std::barrier checkpoint(static_cast<std::ptrdiff_t>(thread_count));
	std::vector<std::thread> workers;
	workers.reserve(thread_count);
	for(size_t thread = 0; thread < thread_count; ++thread)
	{
		workers.emplace_back([&, thread]
		{
			size_t checksum = 0;
			start.arrive_and_wait();
			for(size_t index = 0; index < iterations; ++index)
			{
				if( write_interval != 0 and (index + thread) % write_interval == 0 )
				{
					std::unique_lock lock(mutex);
					++value;
				}
				else
				{
					std::shared_lock lock(mutex);
					checksum += value;
				}
				if( (index + 1) % checkpoint_interval == 0 )
					checkpoint.arrive_and_wait();
			}
			if( checksum == std::numeric_limits<size_t>::max() )
				std::abort();
		});
	}
	const auto begin = std::chrono::steady_clock::now();
	start.arrive_and_wait();
	for(auto &worker : workers)
		worker.join();
	return std::chrono::steady_clock::now() - begin;
}

void mutex_contention()
{
	measure_and_print(
		"std::mutex uncontended", iterations,
		measure_uncontended<std::mutex>, "lock"
	);
	measure_and_print(
		"atomic_mutex uncontended", iterations,
		measure_uncontended<riwo::atomic_mutex>, "lock"
	);
	measure_and_print(
		"spin_mutex uncontended", iterations,
		measure_uncontended<riwo::spin_mutex>, "lock"
	);
	for(const size_t threads : {2U, 4U, 8U})
	{
		for(const size_t work : {0U, 16U})
		{
			measure_and_print(
				std::format("std::mutex {} threads, work {}", threads, work),
				threads * iterations,
				[=] {
					return measure_exclusive<std::mutex>(threads, work);
				}, "lock"
			);
			measure_and_print(
				std::format("atomic_mutex {} threads, work {}", threads, work),
				threads * iterations,
				[=] {
					return measure_exclusive<riwo::atomic_mutex>(threads, work);
				}, "lock"
			);
			measure_and_print(
				std::format("spin_mutex {} threads, work {}", threads, work),
				threads * iterations,
				[=] {
					return measure_exclusive<riwo::spin_mutex>(threads, work);
				}, "lock"
			);
		}
	}
}

void shared_mutex_contention()
{
	for(const bool shared : {false, true})
	{
		const auto suffix = shared ? "shared" : "exclusive";
		measure_and_print(
			std::format("std::shared_mutex uncontended {}", suffix), iterations,
			[=] {
				return shared ? measure_uncontended<std::shared_mutex,true>() :
					measure_uncontended<std::shared_mutex>();
			}, "lock"
		);
		measure_and_print(
			std::format("atomic_shared_mutex uncontended {}", suffix), iterations,
			[=] {
				return shared ? measure_uncontended<riwo::atomic_shared_mutex,true>() :
					measure_uncontended<riwo::atomic_shared_mutex>();
			}, "lock"
		);
		measure_and_print(
			std::format("spin_shared_mutex uncontended {}", suffix), iterations,
			[=] {
				return shared ? measure_uncontended<riwo::spin_shared_mutex,true>() :
					measure_uncontended<riwo::spin_shared_mutex>();
			}, "lock"
		);
	}
	for(const size_t threads : {2U, 4U, 8U})
	{
		for(const size_t write_interval : {size_t {0}, size_t {1}, size_t {10}})
		{
			const auto standard_name = std::format(
				"std::shared_mutex {} threads, write interval {}",
				threads, write_interval
			);
#if defined(__MINGW32__)
			if( write_interval > 1 )
			{
				// libstdc++ implements std::shared_mutex with winpthreads.  Its
				// pthread_rwlock can deadlock internally under mixed reader/writer
				// contention (threads remain in rdlock, wrlock and unlock).  Pure
				// reader/writer baselines and all Riwo mixed tests remain enabled.
				std::cout << "[SKIP] " << standard_name
					<< ": MinGW winpthreads mixed rwlock contention can deadlock"
					<< std::endl;
			}
			else
#endif
			{
				measure_and_print(
					standard_name, threads * iterations,
					[=] {
						return measure_shared<std::shared_mutex>(threads, write_interval);
					}, "lock"
				);
			}
			measure_and_print(
				std::format("atomic_shared_mutex {} threads, write interval {}",
					threads, write_interval),
				threads * iterations,
				[=] {
					return measure_shared<riwo::atomic_shared_mutex>(threads, write_interval);
				}, "lock"
			);
			measure_and_print(
				std::format("spin_shared_mutex {} threads, write interval {}",
					threads, write_interval),
				threads * iterations,
				[=] {
					return measure_shared<riwo::spin_shared_mutex>(threads, write_interval);
				}, "lock"
			);
		}
	}
}

} //namespace

int main(int argc, const char *const argv[])
{
	// Keep glibc's single-thread-only pthread fast path out of the comparison.
	std::thread([] {}).join();
	return riwo::test::run(argc, argv, {
		{"mutex contention", mutex_contention},
		{"shared mutex contention", shared_mutex_contention},
	});
}
