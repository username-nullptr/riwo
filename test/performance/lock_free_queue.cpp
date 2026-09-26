// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/core/lock_free_queue.h>

namespace
{

#ifdef NDEBUG
constexpr size_t operation_count = 1'000'000 * riwo::test::performance_scale;
#else
constexpr size_t operation_count = 100'000 * riwo::test::performance_scale;
#endif

template <riwo::queue_type Type>
std::chrono::steady_clock::duration measure_spsc_queue(size_t item_count)
{
	riwo::lock_free_queue<size_t,Type> queue(65'536);
	std::atomic_uint ready = 0;
	std::atomic_bool start = false;
	std::uint64_t checksum = 0;

	std::thread producer([&]
	{
		ready.fetch_add(1, std::memory_order_release);
		while(not start.load(std::memory_order_acquire))
			std::this_thread::yield();

		for(size_t value = 1; value <= item_count; ++value)
		{
			while(not queue.enqueue(value))
				std::this_thread::yield();
		}
	});

	std::thread consumer([&]
	{
		ready.fetch_add(1, std::memory_order_release);
		while(not start.load(std::memory_order_acquire))
			std::this_thread::yield();

		for(size_t index = 0; index < item_count;)
		{
			if(auto value = queue.dequeue())
			{
				checksum += *value;
				++index;
			}
			else
				std::this_thread::yield();
		}
	});

	while(ready.load(std::memory_order_acquire) != 2)
		std::this_thread::yield();
	const auto begin = std::chrono::steady_clock::now();
	start.store(true, std::memory_order_release);
	producer.join();
	consumer.join();
	const auto elapsed = std::chrono::steady_clock::now() - begin;

	const auto expected = static_cast<std::uint64_t>(item_count) *
		(static_cast<std::uint64_t>(item_count) + 1) / 2;
	RIWO_TEST_CHECK_EQ(checksum, expected);
	RIWO_TEST_CHECK(queue.empty());
	return elapsed;
}

template <riwo::queue_type Type>
std::chrono::steady_clock::duration measure_mpmc_queue(size_t item_count)
{
	constexpr size_t producer_count = 4;
	constexpr size_t consumer_count = 4;
	const auto items_per_producer = item_count / producer_count;
	item_count = items_per_producer * producer_count;

	riwo::lock_free_queue<size_t,Type> queue(65'536);
	std::atomic_uint ready = 0;
	std::atomic_uint producers_left = producer_count;
	std::atomic_bool start = false;
	std::array<std::uint64_t,consumer_count> checksums {};
	std::array<size_t,consumer_count> consumed {};
	std::array<std::thread,producer_count> producers;
	std::array<std::thread,consumer_count> consumers;

	for(size_t producer = 0; producer < producer_count; ++producer)
	{
		producers[producer] = std::thread([&, producer]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();

			const auto begin = producer * items_per_producer + 1;
			const auto end = begin + items_per_producer;
			for(size_t value = begin; value < end; ++value)
			{
				while(not queue.enqueue(value))
					std::this_thread::yield();
			}
			producers_left.fetch_sub(1, std::memory_order_release);
		});
	}
	for(size_t consumer = 0; consumer < consumer_count; ++consumer)
	{
		consumers[consumer] = std::thread([&, consumer]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();

			for(;;)
			{
				if(auto value = queue.dequeue())
				{
					checksums[consumer] += *value;
					consumed[consumer]++;
				}
				else if(producers_left.load(std::memory_order_acquire) == 0 and
					queue.empty())
					break;
				else
					std::this_thread::yield();
			}
		});
	}

	while(ready.load(std::memory_order_acquire) != producer_count + consumer_count)
		std::this_thread::yield();
	const auto begin = std::chrono::steady_clock::now();
	start.store(true, std::memory_order_release);
	for(auto &producer : producers)
		producer.join();
	for(auto &consumer : consumers)
		consumer.join();
	const auto elapsed = std::chrono::steady_clock::now() - begin;

	std::uint64_t checksum = 0;
	size_t consumed_count = 0;
	for(auto value : checksums)
		checksum += value;
	for(auto value : consumed)
		consumed_count += value;
	const auto expected = static_cast<std::uint64_t>(item_count) *
		(static_cast<std::uint64_t>(item_count) + 1) / 2;
	RIWO_TEST_CHECK_EQ(checksum, expected);
	RIWO_TEST_CHECK_EQ(consumed_count, item_count);
	RIWO_TEST_CHECK(queue.empty());
	return elapsed;
}

void lock_free_queue_throughput()
{
	constexpr size_t sample_count = 3;
	measure_spsc_queue<riwo::queue_type::linked>(operation_count / 10);
	measure_spsc_queue<riwo::queue_type::circular>(operation_count / 10);

	std::array<std::chrono::steady_clock::duration,sample_count> linked {};
	std::array<std::chrono::steady_clock::duration,sample_count> circular {};
	for(size_t sample = 0; sample < sample_count; ++sample)
	{
		if(sample % 2 == 0)
		{
			linked[sample] = measure_spsc_queue<riwo::queue_type::linked>(operation_count);
			circular[sample] = measure_spsc_queue<riwo::queue_type::circular>(operation_count);
		}
		else
		{
			circular[sample] = measure_spsc_queue<riwo::queue_type::circular>(operation_count);
			linked[sample] = measure_spsc_queue<riwo::queue_type::linked>(operation_count);
		}
	}
	std::ranges::sort(linked);
	std::ranges::sort(circular);
	riwo::test::print_performance_result(
		"lock-free queue/linked SPSC (median of 3)", operation_count,
		linked[sample_count / 2], "item"
	);
	riwo::test::print_performance_result(
		"lock-free queue/circular SPSC (median of 3)", operation_count,
		circular[sample_count / 2], "item"
	);

	measure_mpmc_queue<riwo::queue_type::linked>(operation_count / 10);
	measure_mpmc_queue<riwo::queue_type::circular>(operation_count / 10);
	for(size_t sample = 0; sample < sample_count; ++sample)
	{
		if(sample % 2 == 0)
		{
			linked[sample] = measure_mpmc_queue<riwo::queue_type::linked>(operation_count);
			circular[sample] = measure_mpmc_queue<riwo::queue_type::circular>(operation_count);
		}
		else
		{
			circular[sample] = measure_mpmc_queue<riwo::queue_type::circular>(operation_count);
			linked[sample] = measure_mpmc_queue<riwo::queue_type::linked>(operation_count);
		}
	}
	std::ranges::sort(linked);
	std::ranges::sort(circular);
	riwo::test::print_performance_result(
		"lock-free queue/linked MPMC 4P/4C (median of 3)", operation_count,
		linked[sample_count / 2], "item"
	);
	riwo::test::print_performance_result(
		"lock-free queue/circular MPMC 4P/4C (median of 3)", operation_count,
		circular[sample_count / 2], "item"
	);
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"lock-free queue throughput", lock_free_queue_throughput},
	});
}
