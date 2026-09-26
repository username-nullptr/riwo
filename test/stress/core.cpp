// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/core/algorithm/misc.h>
#include <riwo/core/lock_free_queue.h>
#include <riwo/core/url.h>

#include <numeric>

namespace
{

constexpr size_t scale = RIWO_STRESS_SCALE;

template <typename Queue, typename ConcurrentWork>
void run_mpmc_queue_pressure(Queue &queue, ConcurrentWork &&concurrent_work)
{
	constexpr size_t producer_count = 4;
	constexpr size_t consumer_count = 4;
	const size_t values_per_producer = 25'000 * scale;
	const size_t total_values = producer_count * values_per_producer;
	std::atomic_size_t ready {0};
	std::atomic_size_t producers_left {producer_count};
	std::atomic_bool start {false};
	std::atomic_bool corrupt {false};
	std::array<std::thread,producer_count> producers;
	std::array<std::thread,consumer_count> consumers;
	std::array<uint64_t,consumer_count> checksums {};
	std::array<size_t,consumer_count> counts {};
	auto seen = std::make_unique<std::atomic_uint8_t[]>(total_values);
	for(size_t index = 0; index < total_values; ++index)
		seen[index].store(0, std::memory_order_relaxed);

	for(size_t producer = 0; producer < producer_count; ++producer)
	{
		producers[producer] = std::thread([&, producer]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			const auto first = producer * values_per_producer + 1;
			for(size_t offset = 0; offset < values_per_producer; ++offset)
			{
				while(not queue.enqueue(first + offset))
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
					counts[consumer]++;
					if(*value == 0 or *value > total_values)
						corrupt.store(true, std::memory_order_relaxed);
					else if(seen[*value - 1].fetch_add(1,
						std::memory_order_relaxed) != 0)
						corrupt.store(true, std::memory_order_relaxed);
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
	start.store(true, std::memory_order_release);
	concurrent_work(producers_left);
	for(auto &thread : producers)
		thread.join();
	for(auto &thread : consumers)
		thread.join();

	const auto count = std::accumulate(counts.begin(), counts.end(), size_t {0});
	const auto checksum = std::accumulate(checksums.begin(), checksums.end(), uint64_t {0});
	const auto expected = static_cast<uint64_t>(total_values) * (total_values + 1) / 2;
	RIWO_TEST_CHECK_EQ(count, total_values);
	RIWO_TEST_CHECK_EQ(checksum, expected);
	RIWO_TEST_CHECK(not corrupt.load(std::memory_order_relaxed));
	for(size_t index = 0; index < total_values; ++index)
		RIWO_TEST_CHECK_EQ(seen[index].load(std::memory_order_relaxed), 1U);
	RIWO_TEST_CHECK(queue.empty());
}

template <typename Queue>
void run_mpmc_queue_pressure(Queue &queue)
{
	run_mpmc_queue_pressure(queue, [](const auto&) {});
}

template <typename Queue>
void run_queue_lifecycle_repetition(size_t rounds)
{
	constexpr size_t values_per_round = 8;
	const auto base_seed = riwo::test::current_seed();
	for(size_t round = 0; round < rounds; ++round)
	{
		Queue queue(2);
		std::array<size_t,values_per_round> observed {};
		std::atomic_bool start {false};
		std::thread producer([&, round]
		{
			riwo::test::random_sequence random(base_seed ^ round);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			for(size_t offset = 0; offset < values_per_round; ++offset)
			{
				while(not queue.enqueue(round * values_per_round + offset))
					std::this_thread::yield();
				if(random.bounded(4) == 0)
					std::this_thread::yield();
			}
		});
		std::thread consumer([&, round]
		{
			riwo::test::random_sequence random(~base_seed ^ round);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			for(size_t offset = 0; offset < values_per_round;)
			{
				if(auto value = queue.dequeue())
					observed[offset++] = *value;
				else
					std::this_thread::yield();
				if(random.bounded(4) == 0)
					std::this_thread::yield();
			}
		});
		start.store(true, std::memory_order_release);
		producer.join();
		consumer.join();
		for(size_t offset = 0; offset < values_per_round; ++offset)
			RIWO_TEST_CHECK_EQ(observed[offset], round * values_per_round + offset);
		RIWO_TEST_CHECK(queue.empty());
	}
}

void low_load_queue_lifecycle_repetition()
{
	const size_t rounds_per_queue = 128 * scale;
	run_queue_lifecycle_repetition<riwo::circular_lock_free_queue<size_t>>(
		rounds_per_queue);
	run_queue_lifecycle_repetition<riwo::linked_lock_free_queue<size_t>>(
		rounds_per_queue);
}

void circular_queue_saturation_pressure()
{
	riwo::circular_lock_free_queue<uint64_t> queue(1);
	run_mpmc_queue_pressure(queue);
}

void circular_queue_growth_backlog_pressure()
{
	constexpr size_t maximum_capacity = 65'536;
	riwo::circular_lock_free_queue<uint64_t> queue(1);
	size_t next = 0;

	for(size_t capacity = 1; capacity <= maximum_capacity; capacity *= 2)
	{
		queue.set_capacity(capacity);
		while( queue.enqueue(next) )
			next++;
		RIWO_TEST_CHECK_EQ(next, capacity);
		RIWO_TEST_CHECK_EQ(queue.size(), capacity);
	}
	for(size_t expected = 0; expected < maximum_capacity; ++expected)
	{
		auto value = queue.dequeue();
		RIWO_TEST_CHECK(value);
		RIWO_TEST_CHECK_EQ(*value, expected);
	}
	RIWO_TEST_CHECK(queue.empty());

	queue.set_capacity(64);
	RIWO_TEST_CHECK(queue.compact());
	RIWO_TEST_CHECK(not queue.compact());
	RIWO_TEST_CHECK(queue.enqueue(maximum_capacity));
	auto value = queue.dequeue();
	RIWO_TEST_CHECK(value);
	RIWO_TEST_CHECK_EQ(*value, maximum_capacity);
}

void circular_queue_resize_pressure()
{
	riwo::circular_lock_free_queue<uint64_t> queue(1);
	run_mpmc_queue_pressure(queue, [&](const auto &producers_left)
	{
		constexpr size_t maintainer_count = 2;
		std::array<std::thread,maintainer_count> maintainers;

		for(size_t index = 0; index < maintainer_count; ++index)
		{
			maintainers[index] = std::thread([&, index]
			{
				size_t capacity = size_t {1} << index;
				size_t compactions = 0;

				while( producers_left.load(std::memory_order_acquire) != 0 )
				{
					capacity = capacity < 4096 ? capacity * 2 : 1;
					queue.set_capacity(capacity);
					if( capacity == 1 and compactions < 4 )
					{
						(void)queue.compact();
						compactions++;
					}
					for(size_t spin = 0; spin < 32; ++spin)
						std::this_thread::yield();
				}
			});
		}
		for(auto &maintainer : maintainers)
			maintainer.join();
	});
}

void linked_queue_reclamation_pressure()
{
	riwo::linked_lock_free_queue<uint64_t> queue(1);
	run_mpmc_queue_pressure(queue);
}

void forced_eviction_pressure()
{
	constexpr size_t producer_count = 4;
	constexpr size_t consumer_count = 2;
	const size_t values_per_producer = 20'000 * scale;
	const size_t total_values = producer_count * values_per_producer;
	riwo::linked_lock_free_queue<size_t> queue(32);
	std::atomic_size_t ready {0};
	std::atomic_size_t producers_left {producer_count};
	std::atomic_size_t evicted {0};
	std::atomic_size_t consumed {0};
	std::atomic_bool start {false};
	std::atomic_bool corrupt {false};
	auto seen = std::make_unique<std::atomic_uint8_t[]>(total_values);
	for(size_t index = 0; index < total_values; ++index)
		seen[index].store(0, std::memory_order_relaxed);

	std::array<std::thread,producer_count> producers;
	for(size_t producer = 0; producer < producer_count; ++producer)
	{
		producers[producer] = std::thread([&, producer]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			const auto first = producer * values_per_producer;
			for(size_t offset = 0; offset < values_per_producer; ++offset)
			{
				evicted.fetch_add(queue.force_emplace(first + offset),
					std::memory_order_relaxed);
			}
			producers_left.fetch_sub(1, std::memory_order_release);
		});
	}
	std::array<std::thread,consumer_count> consumers;
	for(auto &consumer : consumers)
	{
		consumer = std::thread([&]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			for(;;)
			{
				if(auto value = queue.dequeue())
				{
					consumed.fetch_add(1, std::memory_order_relaxed);
					if(*value >= total_values or
						seen[*value].fetch_add(1, std::memory_order_relaxed) != 0)
						corrupt.store(true, std::memory_order_relaxed);
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
	start.store(true, std::memory_order_release);
	for(auto &thread : producers)
		thread.join();
	for(auto &thread : consumers)
		thread.join();
	RIWO_TEST_CHECK(not corrupt.load(std::memory_order_relaxed));
	RIWO_TEST_CHECK_EQ(consumed.load(std::memory_order_relaxed) +
		evicted.load(std::memory_order_relaxed), total_values);
	RIWO_TEST_CHECK(queue.empty());
}

void concurrent_text_and_url_pressure()
{
	constexpr size_t thread_count = 8;
	const size_t iterations = 10'000 * scale;
	std::atomic_size_t completed {0};
	std::atomic_bool corrupt {false};
	std::array<std::thread,thread_count> threads;
	for(size_t thread_index = 0; thread_index < thread_count; ++thread_index)
	{
		threads[thread_index] = std::thread([&, thread_index]
		{
			for(size_t index = 0; index < iterations; ++index)
			{
				const auto text = std::format("worker {} / item {} ? %", thread_index, index);
				const auto encoded = riwo::to_percent_encoding(text);
				if(riwo::from_percent_encoding(encoded) != text)
					corrupt.store(true, std::memory_order_relaxed);
				riwo::url value("https://example.test:8443/api/{}/items/{}?q={}",
					thread_index, index, encoded);
				if(not value.is_valid())
					corrupt.store(true, std::memory_order_relaxed);
				riwo::url copy(value.to_string());
				if(not copy.is_valid() or copy.to_string() != value.to_string())
					corrupt.store(true, std::memory_order_relaxed);
			}
			completed.fetch_add(1, std::memory_order_release);
		});
	}
	for(auto &thread : threads)
		thread.join();
	RIWO_TEST_CHECK_EQ(completed.load(std::memory_order_acquire), thread_count);
	RIWO_TEST_CHECK(not corrupt.load(std::memory_order_relaxed));
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"low-load queue lifecycle repetition", low_load_queue_lifecycle_repetition},
		{"circular queue saturation pressure", circular_queue_saturation_pressure},
		{"circular queue growth backlog pressure", circular_queue_growth_backlog_pressure},
		{"circular queue resize pressure", circular_queue_resize_pressure},
		{"linked queue reclamation pressure", linked_queue_reclamation_pressure},
		{"forced queue eviction pressure", forced_eviction_pressure},
		{"concurrent text and URL pressure", concurrent_text_and_url_pressure},
	});
}
