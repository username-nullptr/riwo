// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "benchmark.h"
#include "test.h"

#include <riwo/utils/sbus.h>

namespace
{

using steady_clock_t = std::chrono::steady_clock;
using duration_t = steady_clock_t::duration;

#ifdef NDEBUG
constexpr size_t no_subscriber_publish_count =
	1'000'000 * riwo::test::performance_scale;
constexpr size_t subscribed_publish_count =
	20'000 * riwo::test::performance_scale;
constexpr size_t connection_cycle_count =
	1'000 * riwo::test::performance_scale;
constexpr size_t payload_64k_publish_count =
	4'096 * riwo::test::performance_scale;
constexpr size_t payload_1m_publish_count =
	256 * riwo::test::performance_scale;
constexpr size_t payload_64k_fanout_publish_count =
	1'024 * riwo::test::performance_scale;
constexpr size_t payload_1m_fanout_publish_count =
	128 * riwo::test::performance_scale;
#else
constexpr size_t no_subscriber_publish_count =
	100'000 * riwo::test::performance_scale;
constexpr size_t subscribed_publish_count =
	2'000 * riwo::test::performance_scale;
constexpr size_t connection_cycle_count =
	100 * riwo::test::performance_scale;
constexpr size_t payload_64k_publish_count =
	512 * riwo::test::performance_scale;
constexpr size_t payload_1m_publish_count =
	32 * riwo::test::performance_scale;
constexpr size_t payload_64k_fanout_publish_count =
	128 * riwo::test::performance_scale;
constexpr size_t payload_1m_fanout_publish_count =
	16 * riwo::test::performance_scale;
#endif

constexpr size_t publish_batch_size = 64;
constexpr size_t max_batch_payload_bytes = 4 * 1'024 * 1'024;
constexpr size_t sample_count = 3;
constexpr size_t unrelated_interface_count = 32;
constexpr std::string_view topic = "riwo.performance.sbus";

struct measurement
{
	duration_t publish_elapsed {};
	duration_t end_to_end_elapsed {};
	bool completed = true;
	size_t received = 0;
};

bool wait_until_received(const std::atomic_size_t &received, size_t expected)
{
	const auto deadline = steady_clock_t::now() + std::chrono::seconds(10);
	while( received.load(std::memory_order_acquire) < expected )
	{
		if( steady_clock_t::now() >= deadline )
			return false;
		std::this_thread::yield();
	}
	return true;
}

duration_t measure_without_subscribers(size_t payload_size)
{
	std::vector<std::byte> payload(payload_size, std::byte {0x2a});
	for(size_t index = 0; index < 1'000; ++index)
		riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
			topic, payload.data(), payload.size()
		);

	const auto begin = steady_clock_t::now();
	for(size_t index = 0; index < no_subscriber_publish_count; ++index)
		riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
			topic, payload.data(), payload.size()
		);
	return steady_clock_t::now() - begin;
}

duration_t measure_with_unrelated_interfaces(size_t payload_size)
{
	std::vector<std::shared_ptr<riwo::utils::sbus::local_interface>> interfaces;
	interfaces.reserve(unrelated_interface_count);
	for(size_t index = 0; index < unrelated_interface_count; ++index)
	{
		auto interface = std::make_shared<riwo::utils::sbus::local_interface>();
		interface->subscribe(
			"riwo.performance.sbus.unrelated." + std::to_string(index),
			[](const void*, size_t) {}
		);
		interfaces.emplace_back(std::move(interface));
	}

	std::vector<std::byte> payload(payload_size, std::byte {0x2a});
	for(size_t index = 0; index < 1'000; ++index)
		riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
			topic, payload.data(), payload.size()
		);

	const auto begin = steady_clock_t::now();
	for(size_t index = 0; index < no_subscriber_publish_count; ++index)
		riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
			topic, payload.data(), payload.size()
		);
	const auto elapsed = steady_clock_t::now() - begin;
	for(auto &interface : interfaces)
		interface->cancel();
	return elapsed;
}

duration_t measure_connection_cycles()
{
	auto interface = std::make_shared<riwo::utils::sbus::local_interface>();
	const auto begin = steady_clock_t::now();
	for(size_t index = 0; index < connection_cycle_count; ++index)
	{
		interface->subscribe(topic, [](const void*, size_t) {});
		interface->cancel();
	}
	return steady_clock_t::now() - begin;
}

duration_t measure_awaitable_signal()
{
	riwo::io_context_t context;
	riwo::utils::signal<riwo::awaitable<void>(std::vector<std::byte>)> received;
	size_t received_count = 0;
	received.connect([&](const std::vector<std::byte> &payload) {
		if( payload.size() == 8 )
			++received_count;
	});

	auto future = asio::co_spawn(context, [&]() -> riwo::awaitable<void>
	{
		std::vector<std::byte> payload(8, std::byte {0x2a});
		for(size_t index = 0; index < subscribed_publish_count; ++index)
			co_await received(payload);
		co_return ;
	}, riwo::use_future);

	const auto begin = steady_clock_t::now();
	context.run();
	future.get();
	const auto elapsed = steady_clock_t::now() - begin;
	RIWO_TEST_CHECK_EQ(received_count, subscribed_publish_count);
	return elapsed;
}

measurement measure_delivery(
	std::atomic_size_t &received,
	size_t subscriber_count,
	const std::vector<std::byte> &payload,
	size_t publish_count
)
{
	const auto payload_limited_batch_size = payload.empty() ? publish_batch_size :
		std::max<size_t>(1, max_batch_payload_bytes / payload.size());
	const auto batch_size = std::min({
		publish_batch_size, payload_limited_batch_size, publish_count
	});
	for(size_t index = 0; index < batch_size; ++index)
		riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
			topic, payload.data(), payload.size()
		);
	bool completed = wait_until_received(received, batch_size * subscriber_count);
	received.store(0, std::memory_order_relaxed);

	duration_t publish_elapsed {};
	const auto begin = steady_clock_t::now();
	size_t published = 0;
	while( completed and published < publish_count )
	{
		const auto count = std::min(batch_size, publish_count - published);
		const auto publish_begin = steady_clock_t::now();
		for(size_t index = 0; index < count; ++index)
			riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
				topic, payload.data(), payload.size()
			);
		publish_elapsed += steady_clock_t::now() - publish_begin;

		published += count;
		completed = wait_until_received(received, published * subscriber_count);
	}
	const auto end_to_end_elapsed = steady_clock_t::now() - begin;
	return {
		publish_elapsed,
		end_to_end_elapsed,
		completed,
		received.load(std::memory_order_relaxed)
	};
}

measurement measure_subscribed(
	size_t subscriber_count,
	size_t payload_size,
	bool global_subscription,
	size_t publish_count = subscribed_publish_count
)
{
	auto interface = std::make_shared<riwo::utils::sbus::local_interface>();
	std::atomic_size_t received {0};
	std::atomic_bool invalid_payload {false};

	for(size_t index = 0; index < subscriber_count; ++index)
	{
		if( global_subscription )
		{
			interface->subscribe([&, payload_size]
			(std::string_view received_topic, const void*, size_t size)
			{
				if( received_topic != topic or size != payload_size )
					invalid_payload.store(true, std::memory_order_relaxed);
				received.fetch_add(1, std::memory_order_release);
			});
		}
		else
		{
			interface->subscribe(topic, [&, payload_size](const void*, size_t size)
			{
				if( size != payload_size )
					invalid_payload.store(true, std::memory_order_relaxed);
				received.fetch_add(1, std::memory_order_release);
			});
		}
	}

	std::vector<std::byte> payload(payload_size, std::byte {0x2a});
	auto result = measure_delivery(received, subscriber_count, payload, publish_count);
	interface->cancel();
	interface.reset();
	RIWO_TEST_CHECK(result.completed);
	RIWO_TEST_CHECK_EQ(result.received, publish_count * subscriber_count);
	RIWO_TEST_CHECK(not invalid_payload.load(std::memory_order_relaxed));
	return result;
}

measurement measure_local_subscriber(size_t payload_size)
{
	asio::thread_pool pool(1);
	riwo::utils::sbus::local_subscriber subscriber(pool);
	std::atomic_size_t received {0};
	std::atomic_bool invalid_payload {false};
	subscriber.subscribe(topic, [&, payload_size](const void*, size_t size)
	{
		if( size != payload_size )
			invalid_payload.store(true, std::memory_order_relaxed);
		received.fetch_add(1, std::memory_order_release);
	});

	std::vector<std::byte> payload(payload_size, std::byte {0x2a});
	auto result = measure_delivery(received, 1, payload, subscribed_publish_count);
	subscriber.cancel();
	pool.stop();
	pool.join();
	RIWO_TEST_CHECK(result.completed);
	RIWO_TEST_CHECK_EQ(result.received, subscribed_publish_count);
	RIWO_TEST_CHECK(not invalid_payload.load(std::memory_order_relaxed));
	return result;
}

template <typename Func>
duration_t median_duration(Func &&func)
{
	std::array<duration_t,sample_count> samples {};
	for(auto &sample : samples)
		sample = func();
	std::ranges::sort(samples);
	return samples[sample_count / 2];
}

template <typename Func>
measurement median_measurement(Func &&func)
{
	std::array<duration_t,sample_count> publish_samples {};
	std::array<duration_t,sample_count> end_to_end_samples {};
	for(size_t index = 0; index < sample_count; ++index)
	{
		auto sample = func();
		publish_samples[index] = sample.publish_elapsed;
		end_to_end_samples[index] = sample.end_to_end_elapsed;
	}
	std::ranges::sort(publish_samples);
	std::ranges::sort(end_to_end_samples);
	return {
		publish_samples[sample_count / 2],
		end_to_end_samples[sample_count / 2]
	};
}

void print_payload_result(
	std::string_view name,
	size_t publish_count,
	size_t subscriber_count,
	size_t payload_size,
	duration_t elapsed
)
{
	riwo::test::print_performance_result(name, publish_count, elapsed, "message");
	const auto seconds = std::chrono::duration<double>(elapsed).count();
	const auto delivered_bytes = static_cast<double>(publish_count) *
		static_cast<double>(subscriber_count) * static_cast<double>(payload_size);
	std::cout << "[PERF] " << name << " effective payload: "
		<< delivered_bytes / seconds / (1'024.0 * 1'024.0) << " MiB/s\n";
}

void print_connection_performance()
{
	riwo::test::print_performance_result(
		"sbus/subscribe + cancel (median of 3)", connection_cycle_count,
		median_duration(measure_connection_cycles), "cycle"
	);
}

void sbus_throughput()
{
	riwo::test::print_performance_result(
		"sbus/publish no subscribers (8 B, median of 3)", no_subscriber_publish_count,
		median_duration([] { return measure_without_subscribers(8); }), "message"
	);
	riwo::test::print_performance_result(
		"sbus/publish with 32 unrelated interfaces (8 B, median of 3)",
		no_subscriber_publish_count,
		median_duration([] { return measure_with_unrelated_interfaces(8); }), "message"
	);
	print_connection_performance();
	riwo::test::print_performance_result(
		"sbus/awaitable signal baseline (8 B, median of 3)", subscribed_publish_count,
		median_duration(measure_awaitable_signal), "message"
	);

	const auto topic_one = median_measurement([] { return measure_subscribed(1, 8, false); });
	riwo::test::print_performance_result(
		"sbus/topic 1 subscriber publish (8 B, median of 3)", subscribed_publish_count,
		topic_one.publish_elapsed, "message"
	);
	riwo::test::print_performance_result(
		"sbus/topic 1 subscriber end-to-end (8 B, median of 3)", subscribed_publish_count,
		topic_one.end_to_end_elapsed, "message"
	);

	const auto topic_four = median_measurement([] { return measure_subscribed(4, 8, false); });
	riwo::test::print_performance_result(
		"sbus/topic 4 subscribers publish (8 B, median of 3)", subscribed_publish_count,
		topic_four.publish_elapsed, "message"
	);
	riwo::test::print_performance_result(
		"sbus/topic 4 subscribers end-to-end (8 B, median of 3)", subscribed_publish_count,
		topic_four.end_to_end_elapsed, "message"
	);

	const auto global_one = median_measurement([] { return measure_subscribed(1, 8, true); });
	riwo::test::print_performance_result(
		"sbus/global 1 subscriber publish (8 B, median of 3)", subscribed_publish_count,
		global_one.publish_elapsed, "message"
	);
	riwo::test::print_performance_result(
		"sbus/global 1 subscriber end-to-end (8 B, median of 3)", subscribed_publish_count,
		global_one.end_to_end_elapsed, "message"
	);

	const auto local_one = median_measurement([] { return measure_local_subscriber(8); });
	riwo::test::print_performance_result(
		"sbus/local_subscriber publish (8 B, median of 3)", subscribed_publish_count,
		local_one.publish_elapsed, "message"
	);
	riwo::test::print_performance_result(
		"sbus/local_subscriber end-to-end (8 B, median of 3)", subscribed_publish_count,
		local_one.end_to_end_elapsed, "message"
	);

	const auto payload_1k = median_measurement([] { return measure_subscribed(1, 1'024, false); });
	print_payload_result(
		"sbus/topic 1 subscriber publish (1 KiB, median of 3)", subscribed_publish_count,
		1, 1'024, payload_1k.publish_elapsed
	);
	print_payload_result(
		"sbus/topic 1 subscriber end-to-end (1 KiB, median of 3)", subscribed_publish_count,
		1, 1'024, payload_1k.end_to_end_elapsed
	);

	const auto payload_64k = median_measurement([] {
		return measure_subscribed(1, 64 * 1'024, false, payload_64k_publish_count);
	});
	print_payload_result(
		"sbus/topic 1 subscriber publish (64 KiB, median of 3)", payload_64k_publish_count,
		1, 64 * 1'024, payload_64k.publish_elapsed
	);
	print_payload_result(
		"sbus/topic 1 subscriber end-to-end (64 KiB, median of 3)", payload_64k_publish_count,
		1, 64 * 1'024, payload_64k.end_to_end_elapsed
	);

	const auto payload_1m = median_measurement([] {
		return measure_subscribed(1, 1'024 * 1'024, false, payload_1m_publish_count);
	});
	print_payload_result(
		"sbus/topic 1 subscriber publish (1 MiB, median of 3)", payload_1m_publish_count,
		1, 1'024 * 1'024, payload_1m.publish_elapsed
	);
	print_payload_result(
		"sbus/topic 1 subscriber end-to-end (1 MiB, median of 3)", payload_1m_publish_count,
		1, 1'024 * 1'024, payload_1m.end_to_end_elapsed
	);

	const auto payload_64k_four = median_measurement([] {
		return measure_subscribed(4, 64 * 1'024, false, payload_64k_fanout_publish_count);
	});
	print_payload_result(
		"sbus/topic 4 subscribers publish (64 KiB, median of 3)",
		payload_64k_fanout_publish_count, 4, 64 * 1'024, payload_64k_four.publish_elapsed
	);
	print_payload_result(
		"sbus/topic 4 subscribers end-to-end (64 KiB, median of 3)",
		payload_64k_fanout_publish_count, 4, 64 * 1'024, payload_64k_four.end_to_end_elapsed
	);

	const auto payload_1m_four = median_measurement([] {
		return measure_subscribed(4, 1'024 * 1'024, false, payload_1m_fanout_publish_count);
	});
	print_payload_result(
		"sbus/topic 4 subscribers publish (1 MiB, median of 3)",
		payload_1m_fanout_publish_count, 4, 1'024 * 1'024, payload_1m_four.publish_elapsed
	);
	print_payload_result(
		"sbus/topic 4 subscribers end-to-end (1 MiB, median of 3)",
		payload_1m_fanout_publish_count, 4, 1'024 * 1'024, payload_1m_four.end_to_end_elapsed
	);
}

} //namespace

int main(int argc, const char *const argv[])
{
	if( argc == 2 and std::string_view(argv[1]) == "--connection-only" )
	{
		print_connection_performance();
		return 0;
	}
	return riwo::test::run(argc, argv, {
		{"sbus throughput", sbus_throughput},
	});
}
