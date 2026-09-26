// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/utils/sbus.h>
#include <cstring>

namespace
{

using namespace std::chrono_literals;

static_assert(riwo::test::canonical_executor_type<
	riwo::utils::sbus::local_subscriber>);
static_assert(riwo::test::canonical_executor_type<
	riwo::utils::sbus::local_cache>);
#if RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
static_assert(riwo::utils::sbus::concepts::interface<
	riwo::utils::sbus::udp_interface>);
static_assert(riwo::test::canonical_executor_type<
	riwo::utils::sbus::udp_subscriber>);
static_assert(riwo::test::canonical_executor_type<
	riwo::utils::sbus::udp_cache>);
#endif
#if RIWO_UTILS_SBUS_DEFAULT_INTERFACE_UDP
static_assert(std::same_as<riwo::utils::sbus::default_interface,
	riwo::utils::sbus::udp_interface>);
static_assert(std::same_as<riwo::utils::sbus::default_subscriber,
	riwo::utils::sbus::udp_subscriber>);
#else
static_assert(std::same_as<riwo::utils::sbus::default_interface,
	riwo::utils::sbus::local_interface>);
static_assert(std::same_as<riwo::utils::sbus::default_subscriber,
	riwo::utils::sbus::local_subscriber>);
#endif

bool wait_for_count(const std::atomic_size_t &count, size_t expected)
{
	for(int retry = 0; retry < 2'000 and count.load() < expected; ++retry)
		std::this_thread::sleep_for(1ms);
	return count.load() == expected;
}

void default_interface_delivery()
{
	constexpr std::string_view topic = "riwo.test.sbus.default-interface";
	asio::thread_pool pool(1);
	riwo::utils::sbus::default_subscriber subscriber(pool);
	std::atomic_size_t received {0};
	std::atomic_bool invalid {false};

	subscriber.subscribe(topic, [&](uint32_t value)
	{
		const auto index = received.load(std::memory_order_relaxed);
		const auto expected = index == 0 ? 0x12345678U : 0x87654321U;
		if( value != expected )
			invalid.store(true, std::memory_order_relaxed);
		received.fetch_add(1, std::memory_order_release);
	});
	riwo::utils::sbus::publish(topic, uint32_t {0x12345678U});
	RIWO_TEST_CHECK(wait_for_count(received, 1));

	const uint32_t value = 0x87654321U;
	riwo::utils::sbus::publish(topic, &value, sizeof(value));
	RIWO_TEST_CHECK(wait_for_count(received, 2));
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));

	subscriber.cancel();
	pool.stop();
	pool.join();
}

void delivery_and_cancellation()
{
	constexpr std::string_view topic = "riwo.test.sbus.delivery";
	auto interface = std::make_shared<riwo::utils::sbus::local_interface>();
	std::atomic_size_t topic_received = 0;
	std::atomic_size_t global_received = 0;

	const auto topic_sid = interface->subscribe(topic, [&](const void*, size_t) {
		topic_received.fetch_add(1);
	});
	const auto global_sid = interface->subscribe(
	[&](std::string_view, const void*, size_t) {
		global_received.fetch_add(1);
	});

	interface->cancel_sid(global_sid);
	for(size_t batch = 1; batch <= 100; ++batch)
	{
		for(size_t index = 0; index < 64; ++index)
			riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
				topic, index
			);
		RIWO_TEST_CHECK(wait_for_count(topic_received, batch * 64));
	}
	RIWO_TEST_CHECK_EQ(global_received.load(), 0);

	interface->cancel_sid(topic_sid);
	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(topic, 42);
	std::this_thread::sleep_for(5ms);
	RIWO_TEST_CHECK_EQ(topic_received.load(), 6'400);
}

void large_payload_fanout_owns_one_copy()
{
	constexpr std::string_view topic = "riwo.test.sbus.large-payload";
	constexpr size_t payload_size = 64 * 1'024;
	auto interface = std::make_shared<riwo::utils::sbus::local_interface>();
	std::atomic_size_t received {0};
	std::atomic<const void*> delivered_data {nullptr};
	std::atomic_bool invalid {false};
	std::vector<std::byte> payload(payload_size, std::byte {0x2a});
	const auto *caller_data = payload.data();

	auto callback = [&](const void *data, size_t size)
	{
		if( size != payload_size or data == caller_data )
			invalid.store(true, std::memory_order_relaxed);
		else
		{
			auto expected = static_cast<const void*>(nullptr);
			if( not delivered_data.compare_exchange_strong(expected, data) and expected != data )
				invalid.store(true, std::memory_order_relaxed);
			auto bytes = static_cast<const std::byte*>(data);
			if( bytes[0] != std::byte {0x2a} or bytes[size - 1] != std::byte {0x2a} )
				invalid.store(true, std::memory_order_relaxed);
		}
		received.fetch_add(1, std::memory_order_release);
	};
	interface->subscribe(topic, callback);
	interface->subscribe(topic, callback);

	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
		topic, payload.data(), payload.size()
	);
	payload.clear();
	payload.shrink_to_fit();
	RIWO_TEST_CHECK(wait_for_count(received, 2));
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));
	interface->cancel();
}

void indexed_topics_and_empty_topic_cancellation()
{
	constexpr std::string_view target_topic = "riwo.test.sbus.indexed-target";
	std::vector<std::shared_ptr<riwo::utils::sbus::local_interface>> unrelated;
	std::atomic_size_t unrelated_received {0};
	unrelated.reserve(32);
	for(size_t index = 0; index < 32; ++index)
	{
		auto interface = std::make_shared<riwo::utils::sbus::local_interface>();
		interface->subscribe(
			"riwo.test.sbus.unrelated." + std::to_string(index),
			[&](const void*, size_t) { unrelated_received.fetch_add(1); }
		);
		unrelated.emplace_back(std::move(interface));
	}

	auto target = std::make_shared<riwo::utils::sbus::local_interface>();
	std::atomic_size_t target_received {0};
	const auto target_sid = target->subscribe(target_topic,
	[&](const void*, size_t) {
		target_received.fetch_add(1, std::memory_order_release);
	});

	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
		target_topic, 42
	);
	RIWO_TEST_CHECK(wait_for_count(target_received, 1));
	std::this_thread::sleep_for(5ms);
	RIWO_TEST_CHECK_EQ(unrelated_received.load(), 0U);

	target->cancel_sid(target_sid);
	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>(
		target_topic, 43
	);
	std::this_thread::sleep_for(5ms);
	RIWO_TEST_CHECK_EQ(target_received.load(), 1U);

	// Topic "" and a global subscription are distinct index entries.
	auto empty = std::make_shared<riwo::utils::sbus::local_interface>();
	std::atomic_size_t empty_received {0};
	std::atomic_size_t global_received {0};
	const auto empty_sid = empty->subscribe("", [&](const void*, size_t) {
		empty_received.fetch_add(1, std::memory_order_release);
	});
	empty->subscribe([&](std::string_view, const void*, size_t) {
		global_received.fetch_add(1, std::memory_order_release);
	});

	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>("", 1);
	RIWO_TEST_CHECK(wait_for_count(empty_received, 1));
	RIWO_TEST_CHECK(wait_for_count(global_received, 1));
	empty->cancel_sid(empty_sid);
	riwo::utils::sbus::publish<riwo::utils::sbus::local_interface>("", 2);
	RIWO_TEST_CHECK(wait_for_count(global_received, 2));
	std::this_thread::sleep_for(5ms);
	RIWO_TEST_CHECK_EQ(empty_received.load(), 1U);

	empty->cancel();
	target->cancel();
	for(auto &interface : unrelated)
		interface->cancel();
}

#if RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
class udp_config_guard
{
public:
	udp_config_guard() : m_config(riwo::utils::sbus::udp_interface::config()) {
		riwo::utils::sbus::udp_interface::set_config({});
	}

	~udp_config_guard() {
		riwo::utils::sbus::udp_interface::set_config(m_config);
	}

private:
	riwo::utils::sbus::udp_interface::config_t m_config;
};

void udp_delivery_fragmentation_and_cancellation()
{
	using udp_interface = riwo::utils::sbus::udp_interface;
	using msg_range = udp_interface::msg_range;
	udp_config_guard config_guard;
	constexpr std::string_view topic = "riwo.test.sbus.udp";
	const auto default_config = udp_interface::config();
	RIWO_TEST_CHECK(default_config == udp_interface::config_t {});
	RIWO_TEST_CHECK_EQ(default_config.send_range, msg_range::process);
	RIWO_TEST_CHECK_EQ(default_config.recv_range, msg_range::process);

	auto invalid_config = default_config;
	invalid_config.send_range = static_cast<msg_range>(255);
	RIWO_TEST_CHECK_THROWS(
		udp_interface::set_config(invalid_config), riwo::invalid_argument
	);
	invalid_config = default_config;
	invalid_config.recv_range = static_cast<msg_range>(255);
	RIWO_TEST_CHECK_THROWS(
		udp_interface::set_config(invalid_config), riwo::invalid_argument
	);
	invalid_config = default_config;
	invalid_config.multicast_group = {127, 0, 0, 1};
	RIWO_TEST_CHECK_THROWS(
		udp_interface::set_config(invalid_config), riwo::invalid_argument
	);
	invalid_config = default_config;
	invalid_config.multicast_port = 0;
	RIWO_TEST_CHECK_THROWS(
		udp_interface::set_config(invalid_config), riwo::invalid_argument
	);
	invalid_config = default_config;
	invalid_config.source_prune_datagram_interval = 0;
	RIWO_TEST_CHECK_THROWS(
		udp_interface::set_config(invalid_config), riwo::invalid_argument
	);
	invalid_config = default_config;
	invalid_config.reassembly_check_interval = 0ms;
	RIWO_TEST_CHECK_THROWS(
		udp_interface::set_config(invalid_config), riwo::invalid_argument
	);
	RIWO_TEST_CHECK(udp_interface::config() == default_config);
	auto restricted_config = default_config;
	restricted_config.max_topic_size = 8;
	restricted_config.max_payload_size = 4;
	udp_interface::set_config(restricted_config);
	RIWO_TEST_CHECK_THROWS(
		udp_interface::publish("123456789", nullptr, 0), riwo::length_error
	);
	const std::array<std::byte,5> oversized_payload {};
	RIWO_TEST_CHECK_THROWS(
		udp_interface::publish("topic", oversized_payload.data(),
			oversized_payload.size()),
		riwo::length_error
	);
	auto endpoint_config = default_config;
	endpoint_config.multicast_group = {239, 255, 71, 84};
	endpoint_config.multicast_port = 57'184;
	udp_interface::set_config(endpoint_config);
	auto endpoint_interface = std::make_shared<udp_interface>(endpoint_config);
	std::atomic_size_t endpoint_received {0};
	endpoint_interface->subscribe("riwo.test.sbus.udp.configured-endpoint",
		[&](const void*, size_t) {
			endpoint_received.fetch_add(1, std::memory_order_release);
		});
	udp_interface::publish("riwo.test.sbus.udp.configured-endpoint", nullptr, 0);
	RIWO_TEST_CHECK(wait_for_count(endpoint_received, 1));
	endpoint_interface->cancel();
	udp_interface::set_config(default_config);
	auto topic_interface =
		std::make_shared<udp_interface>();
	auto global_interface =
		std::make_shared<udp_interface>();
	std::atomic_size_t topic_received {0};
	std::atomic_size_t global_received {0};
	std::atomic_bool invalid {false};

	const auto topic_sid = topic_interface->subscribe(topic,
	[&](const void *data, size_t size)
	{
		uint32_t value = 0;
		if( size == sizeof(value) )
			std::memcpy(&value, data, sizeof(value));
		if( size != sizeof(value) or value != 0x12345678U )
		{
			invalid.store(true, std::memory_order_relaxed);
		}
		topic_received.fetch_add(1, std::memory_order_release);
	});
	const auto global_sid = global_interface->subscribe(
	[&](std::string_view received_topic, const void*, size_t)
	{
		if( received_topic != topic )
			invalid.store(true, std::memory_order_relaxed);
		global_received.fetch_add(1, std::memory_order_release);
	});

	riwo::utils::sbus::publish<riwo::utils::sbus::udp_interface>(
		topic, uint32_t {0x12345678U}
	);
	RIWO_TEST_CHECK(wait_for_count(topic_received, 1));
	RIWO_TEST_CHECK(wait_for_count(global_received, 1));
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));
	for(size_t retry = 0; retry < 2'000 and
		topic_interface->statistics().delivered_messages == 0; ++retry)
	{
		std::this_thread::sleep_for(1ms);
	}
	const auto initial_statistics = topic_interface->statistics();
	RIWO_TEST_CHECK(initial_statistics.received_datagrams >= 1);
	RIWO_TEST_CHECK(initial_statistics.delivered_messages >= 1);

	auto config = default_config;
	config.send_range = msg_range::lan;
	udp_interface::set_config(config);
	riwo::utils::sbus::publish<udp_interface>(
		topic, uint32_t {0x12345678U}
	);
	std::this_thread::sleep_for(20ms);
	RIWO_TEST_CHECK_EQ(topic_received.load(), 1U);
	RIWO_TEST_CHECK_EQ(global_received.load(), 1U);

	config.recv_range = msg_range::lan;
	auto lan_interface = std::make_shared<udp_interface>(config);
	std::atomic_size_t lan_received {0};
	lan_interface->subscribe(topic, [&](const void*, size_t) {
		lan_received.fetch_add(1, std::memory_order_release);
	});
	riwo::utils::sbus::publish<udp_interface>(
		topic, uint32_t {0x12345678U}
	);
	RIWO_TEST_CHECK(wait_for_count(lan_received, 1));
	RIWO_TEST_CHECK_EQ(topic_received.load(), 1U);
	RIWO_TEST_CHECK_EQ(global_received.load(), 1U);

	config.send_range = msg_range::internet;
	udp_interface::set_config(config);
	riwo::utils::sbus::publish<udp_interface>(
		topic, uint32_t {0x12345678U}
	);
	std::this_thread::sleep_for(20ms);
	RIWO_TEST_CHECK_EQ(lan_received.load(), 1U);

	config.recv_range = msg_range::internet;
	auto internet_interface = std::make_shared<udp_interface>(config);
	std::atomic_size_t internet_received {0};
	internet_interface->subscribe(topic, [&](const void*, size_t) {
		internet_received.fetch_add(1, std::memory_order_release);
	});
	riwo::utils::sbus::publish<udp_interface>(
		topic, uint32_t {0x12345678U}
	);
	RIWO_TEST_CHECK(wait_for_count(internet_received, 1));
	lan_interface->cancel();
	internet_interface->cancel();
	udp_interface::set_config(default_config);

	topic_interface->cancel_sid(topic_sid);
	riwo::utils::sbus::publish<udp_interface>(
		topic, uint32_t {0x12345678U}
	);
	RIWO_TEST_CHECK(wait_for_count(global_received, 2));
	std::this_thread::sleep_for(20ms);
	RIWO_TEST_CHECK_EQ(topic_received.load(), 1U);

	constexpr size_t payload_size = 192 * 1'024;
	std::vector<std::byte> payload(payload_size);
	for(size_t index = 0; index < payload.size(); ++index)
		payload[index] = static_cast<std::byte>((index * 31) & 0xff);
	std::atomic_size_t fragmented_received {0};
	topic_interface->subscribe(topic, [&](const void *data, size_t size)
	{
		if( size != payload.size() or
			not std::equal(payload.begin(), payload.end(),
				static_cast<const std::byte*>(data)) )
		{
			invalid.store(true, std::memory_order_relaxed);
		}
		fragmented_received.fetch_add(1, std::memory_order_release);
	});
	riwo::utils::sbus::publish<udp_interface>(
		topic, payload.data(), payload.size()
	);
	RIWO_TEST_CHECK(wait_for_count(fragmented_received, 1));
	RIWO_TEST_CHECK(wait_for_count(global_received, 3));
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));

	topic_interface->cancel_topic(topic);
	global_interface->cancel_sid(global_sid);
	udp_interface::publish(topic, nullptr, 0);
	std::this_thread::sleep_for(20ms);
	RIWO_TEST_CHECK_EQ(fragmented_received.load(), 1U);
	RIWO_TEST_CHECK_EQ(global_received.load(), 3U);
	RIWO_TEST_CHECK_THROWS(
		udp_interface::publish(topic, nullptr, 1),
		riwo::invalid_argument
	);
	const std::string oversized_topic(4 * 1'024 + 1, 't');
	RIWO_TEST_CHECK_THROWS(
		udp_interface::publish(oversized_topic, nullptr, 0),
		riwo::length_error
	);

	asio::thread_pool pool(1);
	riwo::utils::sbus::udp_subscriber typed_subscriber(pool);
	std::atomic_size_t typed_received {0};
	typed_subscriber.subscribe(topic, [&](uint64_t value)
	{
		if( value != 0x0123456789abcdefULL )
			invalid.store(true, std::memory_order_relaxed);
		typed_received.fetch_add(1, std::memory_order_release);
	});
	riwo::utils::sbus::publish<udp_interface>(
		topic, uint64_t {0x0123456789abcdefULL}
	);
	RIWO_TEST_CHECK(wait_for_count(typed_received, 1));
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));
	typed_subscriber.cancel();
	pool.stop();
	pool.join();

	topic_interface->cancel();
	global_interface->cancel();
}
#endif

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"sbus selected default interface delivery", default_interface_delivery},
		{"sbus delivery and cancellation", delivery_and_cancellation},
		{"sbus large payload fanout owns one copy", large_payload_fanout_owns_one_copy},
		{"sbus indexed topics and empty-topic cancellation", indexed_topics_and_empty_topic_cancellation},
#if RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
		{"sbus udp delivery, fragmentation, and cancellation",
			udp_delivery_fragmentation_and_cancellation},
#endif
	});
}
