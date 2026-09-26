// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/utils/sbus.h>
#include <riwo/utils/signal_slot.h>

namespace
{

std::atomic_size_t *signal_count = nullptr;

void count_signal(size_t value)
{
	signal_count->fetch_add(value, std::memory_order_relaxed);
}

void low_load_utility_lifecycle_repetition()
{
	const size_t signal_rounds = 5'000 * RIWO_STRESS_SCALE;
	size_t signal_received = 0;
	for(size_t round = 0; round < signal_rounds; ++round)
	{
		riwo::utils::signal<void(size_t)> signal;
		signal.connect([&](size_t value) { signal_received += value; });
		signal(1);
		signal.block();
		signal(1);
		signal.block(false);
		signal.disconnect();
		signal(1);
	}
	RIWO_TEST_CHECK_EQ(signal_received, signal_rounds);

	const size_t bus_rounds = 64 * RIWO_STRESS_SCALE;
	std::atomic_size_t bus_received {0};
	for(size_t round = 0; round < bus_rounds; ++round)
	{
		const auto topic = std::format(
			"riwo.test.stress.lifecycle.{}", round);
		auto interface =
			std::make_shared<riwo::utils::sbus::local_interface>();
		interface->subscribe(topic, [&](const void *data, size_t size)
		{
			if(data != nullptr and size == sizeof(size_t))
				bus_received.fetch_add(1, std::memory_order_release);
		});
		riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
			topic, round
		);
		const auto expected = round + 1;
		for(size_t retry = 0;
			retry < 2'000 and bus_received.load(std::memory_order_acquire) != expected;
			++retry)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		RIWO_TEST_CHECK_EQ(
			bus_received.load(std::memory_order_acquire), expected);
		interface->cancel();
	}
}

void concurrent_signal_pressure()
{
	riwo::utils::signal<void(size_t)> signal;
	std::atomic_size_t received {0};
	signal_count = &received;
	signal.connect(count_signal);
	constexpr size_t emitter_count = 4;
	constexpr size_t mutator_count = 2;
	const size_t emissions_per_thread = 25'000 * RIWO_STRESS_SCALE;
	std::atomic_size_t ready {0};
	std::atomic_bool start {false};
	std::array<std::thread,emitter_count> emitters;
	for(auto &emitter : emitters)
	{
		emitter = std::thread([&]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			for(size_t index = 0; index < emissions_per_thread; ++index)
				signal(1);
		});
	}
	std::array<std::thread,mutator_count> mutators;
	for(auto &mutator : mutators)
	{
		mutator = std::thread([&]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			for(size_t index = 0; index < emissions_per_thread / 20; ++index)
			{
				signal.disconnect();
				signal.connect(count_signal);
			}
		});
	}
	while(ready.load(std::memory_order_acquire) != emitter_count + mutator_count)
		std::this_thread::yield();
	start.store(true, std::memory_order_release);
	for(auto &emitter : emitters)
		emitter.join();
	for(auto &mutator : mutators)
		mutator.join();
	signal.disconnect();
	const auto before = received.load();
	signal(1);
	RIWO_TEST_CHECK_EQ(received.load(), before);
	RIWO_TEST_CHECK(before <= emitter_count * emissions_per_thread);
	signal_count = nullptr;
}

void message_bus_fanout_pressure()
{
	constexpr std::string_view topic = "riwo.test.stress.sbus";
	constexpr size_t subscriber_count = 8;
	const size_t publish_count = 5'000 * RIWO_STRESS_SCALE;
	auto interface = std::make_shared<riwo::utils::sbus::local_interface>();
	std::atomic_size_t received {0};
	for(size_t index = 0; index < subscriber_count; ++index)
	{
		interface->subscribe(topic, [&](const void *data, size_t size)
		{
			if(size == sizeof(uint64_t) and data != nullptr)
				received.fetch_add(1, std::memory_order_release);
		});
	}
	constexpr size_t batch_size = 64;
	for(size_t batch_begin = 0; batch_begin < publish_count;
		batch_begin += batch_size)
	{
		const auto batch_end = std::min(publish_count, batch_begin + batch_size);
		std::array<std::thread,4> publishers;
		for(size_t publisher = 0; publisher < publishers.size(); ++publisher)
		{
			publishers[publisher] = std::thread([&, publisher, batch_begin, batch_end]
			{
				for(size_t index = batch_begin + publisher; index < batch_end;
				index += publishers.size())
				{
					const uint64_t value = index;
					riwo::utils::sbus::publish<
						riwo::utils::sbus::local_interface>(topic, value);
				}
			});
		}
		for(auto &thread : publishers)
			thread.join();
		const auto expected = batch_end * subscriber_count;
		for(size_t retry = 0; retry < 2'000 and received.load() != expected; ++retry)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		RIWO_TEST_CHECK_EQ(received.load(), expected);
	}
	interface->cancel();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"low-load utility lifecycle repetition",
			low_load_utility_lifecycle_repetition},
		{"concurrent signal pressure", concurrent_signal_pressure},
		{"message bus fanout pressure", message_bus_fanout_pressure},
	});
}
