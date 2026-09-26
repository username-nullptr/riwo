// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "test.h"

#include <riwo/utils/sbus.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <thread>

namespace
{

using namespace std::chrono_literals;
using udp_interface = riwo::utils::sbus::udp_interface;
using msg_range = udp_interface::msg_range;
using udp = asio::ip::udp;

constexpr uint32_t wire_magic = 0x4C475342;
constexpr uint8_t wire_version = 2;
constexpr size_t wire_header_size = 48;
constexpr size_t wire_datagram_size = 60 * 1'024;

class udp_config_guard
{
public:
	udp_config_guard() : m_config(udp_interface::config())
	{
		udp_interface::set_config({});
	}

	~udp_config_guard()
	{
		udp_interface::set_config(m_config);
	}

private:
	udp_interface::config_t m_config;
};

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 3s)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while(not predicate() and std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(1ms);
	return predicate();
}

struct burst_message
{
	uint32_t publisher = 0;
	uint32_t sequence = 0;
	uint64_t checksum = 0;
};

[[nodiscard]] uint64_t burst_checksum(uint32_t publisher, uint32_t sequence) noexcept
{
	return (static_cast<uint64_t>(publisher) << 32 | sequence) ^
		0xd6e8feb86659fd93ULL;
}

void concurrent_multicast_burst_and_recovery()
{
	udp_config_guard config_guard;
	constexpr std::string_view topic = "riwo.test.stress.sbus.udp.burst";
	constexpr size_t receiver_count = 4;
	constexpr size_t publisher_count = 4;
	const size_t messages_per_publisher = 500 * RIWO_STRESS_SCALE;

	std::array<std::shared_ptr<udp_interface>,receiver_count> receivers;
	std::array<std::atomic_size_t,receiver_count> received {};
	std::array<std::atomic_size_t,receiver_count> recovered {};
	std::atomic_bool invalid {false};

	for(size_t receiver_index = 0; receiver_index < receiver_count; ++receiver_index)
	{
		receivers[receiver_index] = std::make_shared<udp_interface>();
		receivers[receiver_index]->subscribe(topic,
		[&, receiver_index](const void *data, size_t size)
		{
			burst_message message;
			if(size != sizeof(message) or data == nullptr)
			{
				invalid.store(true, std::memory_order_relaxed);
				return ;
			}
			std::memcpy(&message, data, sizeof(message));
			if(message.checksum != burst_checksum(message.publisher, message.sequence))
				invalid.store(true, std::memory_order_relaxed);
			if(message.publisher == std::numeric_limits<uint32_t>::max())
				recovered[receiver_index].fetch_add(1, std::memory_order_release);
			else
			{
				if(message.publisher >= publisher_count or
					message.sequence >= messages_per_publisher)
				{
					invalid.store(true, std::memory_order_relaxed);
				}
				received[receiver_index].fetch_add(1, std::memory_order_release);
			}
		});
	}

	std::atomic_size_t ready {0};
	std::atomic_bool start {false};
	std::array<std::thread,publisher_count> publishers;
	for(size_t publisher_index = 0; publisher_index < publisher_count; ++publisher_index)
	{
		publishers[publisher_index] = std::thread([&, publisher_index]
		{
			ready.fetch_add(1, std::memory_order_release);
			while(not start.load(std::memory_order_acquire))
				std::this_thread::yield();
			for(size_t sequence = 0; sequence < messages_per_publisher; ++sequence)
			{
				const burst_message message {
					.publisher = static_cast<uint32_t>(publisher_index),
					.sequence = static_cast<uint32_t>(sequence),
					.checksum = burst_checksum(
						static_cast<uint32_t>(publisher_index),
						static_cast<uint32_t>(sequence)
					)
				};
				udp_interface::publish(topic, &message, sizeof(message));
			}
		});
	}
	while(ready.load(std::memory_order_acquire) != publisher_count)
		std::this_thread::yield();
	start.store(true, std::memory_order_release);
	for(auto &publisher : publishers)
		publisher.join();

	RIWO_TEST_CHECK(wait_until([&]
	{
		return std::ranges::all_of(received,
			[](const auto &count) { return count.load(std::memory_order_acquire) > 0; });
	}));

	// UDP may legitimately drop burst traffic. A marker sent after the burst
	// verifies that every receiver drains its queue and continues making progress.
	for(uint32_t attempt = 0; attempt < 1'000; ++attempt)
	{
		if(std::ranges::all_of(recovered,
			[](const auto &count) { return count.load(std::memory_order_acquire) > 0; }))
		{
			break ;
		}
		const burst_message marker {
			.publisher = std::numeric_limits<uint32_t>::max(),
			.sequence = attempt,
			.checksum = burst_checksum(
				std::numeric_limits<uint32_t>::max(), attempt)
		};
		udp_interface::publish(topic, &marker, sizeof(marker));
		std::this_thread::sleep_for(1ms);
	}
	RIWO_TEST_CHECK(std::ranges::all_of(recovered,
		[](const auto &count) { return count.load(std::memory_order_acquire) > 0; }));
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));

	for(auto &receiver : receivers)
		receiver->cancel();
}

void lifecycle_churn_under_traffic()
{
	udp_config_guard config_guard;
	constexpr std::string_view topic = "riwo.test.stress.sbus.udp.lifecycle";
	const size_t rounds = 16 * RIWO_STRESS_SCALE;

	for(size_t round = 0; round < rounds; ++round)
	{
		auto receiver = std::make_shared<udp_interface>();
		std::atomic_size_t received {0};
		std::atomic_bool invalid {false};
		receiver->subscribe(topic, [&](const void *data, size_t size)
		{
			size_t value = 0;
			if(size == sizeof(value) and data != nullptr)
				std::memcpy(&value, data, sizeof(value));
			if(size != sizeof(value) or value != round)
				invalid.store(true, std::memory_order_relaxed);
			received.fetch_add(1, std::memory_order_release);
		});

		for(size_t retry = 0; retry < 100 and received.load(std::memory_order_acquire) == 0;
			++retry)
		{
			udp_interface::publish(topic, &round, sizeof(round));
			std::this_thread::sleep_for(1ms);
		}
		RIWO_TEST_CHECK(received.load(std::memory_order_acquire) > 0);
		RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));
		receiver->cancel();
	}
}

void slow_callback_queue_pressure_and_recovery()
{
	udp_config_guard config_guard;
	constexpr std::string_view topic =
		"riwo.test.stress.sbus.udp.slow-callback";
	constexpr uint64_t recovery_marker = std::numeric_limits<uint64_t>::max();
	auto config = udp_interface::config();
	config.delivery_queue_capacity = 32;
	config.delivery_queue_bytes = 64 * 1'024;
	udp_interface::set_config(config);
	const size_t publish_count = config.delivery_queue_capacity +
		256 * RIWO_STRESS_SCALE;
	auto receiver = std::make_shared<udp_interface>();
	std::atomic_size_t recovered {0};
	std::atomic_bool invalid {false};
	receiver->subscribe(topic, [&](const void *data, size_t size)
	{
		uint64_t value = 0;
		if(size == sizeof(value) and data != nullptr)
			std::memcpy(&value, data, sizeof(value));
		if(size != sizeof(value))
			invalid.store(true, std::memory_order_relaxed);
		else if(value == recovery_marker)
			recovered.fetch_add(1, std::memory_order_release);
		else if(value >= publish_count)
			invalid.store(true, std::memory_order_relaxed);
		else
			std::this_thread::sleep_for(2ms);
	});

	for(uint64_t value = 0; value < publish_count; ++value)
		udp_interface::publish(topic, &value, sizeof(value));
	RIWO_TEST_CHECK(wait_until([&]
	{
		return receiver->statistics().delivery_queue_drops > 0;
	}));

	for(size_t retry = 0; retry < 2'000 and
		recovered.load(std::memory_order_acquire) == 0; ++retry)
	{
		udp_interface::publish(topic, &recovery_marker, sizeof(recovery_marker));
		std::this_thread::sleep_for(2ms);
	}
	const auto statistics = receiver->statistics();
	RIWO_TEST_CHECK(statistics.delivery_queue_drops > 0);
	RIWO_TEST_CHECK(recovered.load(std::memory_order_acquire) > 0);
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));
	receiver->cancel();
}

void write_u16(std::byte *output, uint16_t value) noexcept
{
	output[0] = static_cast<std::byte>(value >> 8);
	output[1] = static_cast<std::byte>(value);
}

void write_u32(std::byte *output, uint32_t value) noexcept
{
	output[0] = static_cast<std::byte>(value >> 24);
	output[1] = static_cast<std::byte>(value >> 16);
	output[2] = static_cast<std::byte>(value >> 8);
	output[3] = static_cast<std::byte>(value);
}

void write_u64(std::byte *output, uint64_t value) noexcept
{
	write_u32(output, static_cast<uint32_t>(value >> 32));
	write_u32(output + 4, static_cast<uint32_t>(value));
}

[[nodiscard]] std::vector<std::byte> make_wire_datagram(
	uint64_t message_id,
	std::string_view topic,
	uint32_t total_size,
	uint32_t index,
	std::span<const std::byte> payload)
{
	const auto capacity = wire_datagram_size - wire_header_size - topic.size();
	const auto count = total_size == 0 ? 1U : static_cast<uint32_t>(
		(total_size + capacity - 1) / capacity);
	const auto offset = static_cast<uint32_t>(index * capacity);
	std::vector<std::byte> datagram(wire_header_size + topic.size() + payload.size());
	auto *header = datagram.data();
	write_u32(header, wire_magic);
	header[4] = static_cast<std::byte>(wire_version);
	header[5] = static_cast<std::byte>(1); // LAN frame, carried with IP TTL 0 below.
	write_u16(header + 6, wire_header_size);
	write_u64(header + 8, message_id);
	write_u32(header + 16, index);
	write_u32(header + 20, count);
	write_u16(header + 24, static_cast<uint16_t>(topic.size()));
	write_u16(header + 26, 0);
	write_u32(header + 28, total_size);
	write_u32(header + 32, offset);
	write_u32(header + 36, static_cast<uint32_t>(payload.size()));
	write_u64(header + 40, 0x535452455353ULL);
	std::ranges::copy(topic,
		reinterpret_cast<char*>(datagram.data() + wire_header_size));
	std::ranges::copy(payload,
		datagram.begin() + static_cast<std::ptrdiff_t>(wire_header_size + topic.size()));
	return datagram;
}

void malformed_and_incomplete_fragment_storm_recovery()
{
	udp_config_guard config_guard;
	auto config = udp_interface::config();
	config.send_range = msg_range::lan;
	config.recv_range = msg_range::lan;
	config.source_datagram_burst = 1'024;
	config.max_reassemblies = 32;
	config.max_reassembly_bytes = 16 * 1'024 * 1'024;
	config.max_source_reassemblies = 4;
	config.max_source_reassembly_bytes = 8 * 1'024 * 1'024;
	udp_interface::set_config(config);
	constexpr std::string_view probe_topic =
		"riwo.test.stress.sbus.udp.fragment-storm.probe";
	constexpr std::string_view entry_limit_topic =
		"riwo.test.stress.sbus.udp.fragment-storm.entry-limit";
	constexpr std::string_view byte_limit_topic =
		"riwo.test.stress.sbus.udp.fragment-storm.byte-limit";
	constexpr std::string_view unmatched_topic =
		"riwo.test.stress.sbus.udp.fragment-storm.unmatched";
	auto receiver = std::make_shared<udp_interface>();
	std::atomic_size_t recovered {0};
	std::atomic_bool invalid {false};
	constexpr uint64_t probe_value = 0xdecafbad12345678ULL;
	receiver->subscribe(probe_topic, [&](const void *data, size_t size)
	{
		uint64_t value = 0;
		if(size == sizeof(value) and data != nullptr)
			std::memcpy(&value, data, sizeof(value));
		if(size != sizeof(value) or value != probe_value)
			invalid.store(true, std::memory_order_relaxed);
		recovered.fetch_add(1, std::memory_order_release);
	});
	receiver->subscribe(entry_limit_topic, [](const void*, size_t) {});
	receiver->subscribe(byte_limit_topic, [](const void*, size_t) {});

	asio::io_context context;
	udp::socket socket(context, udp::v4());
	socket.set_option(asio::ip::multicast::enable_loopback(true));
	// This is the safety boundary: injected stress traffic cannot leave the host.
	socket.set_option(asio::ip::multicast::hops(0));
	socket.set_option(asio::socket_base::send_buffer_size(4 * 1'024 * 1'024));
	const udp::endpoint destination(
		asio::ip::address_v4(config.multicast_group), config.multicast_port);

	// Valid first fragments that never complete cross both per-source
	// reassembly limits, even with RIWO_STRESS_SCALE=1.
	auto send_incomplete_assemblies = [&] (
		std::string_view topic, uint32_t assembly_size, size_t count,
		uint64_t first_message_id)
	{
		const auto first_fragment_size =
			wire_datagram_size - wire_header_size - topic.size();
		std::vector<std::byte> fragment_payload(
			first_fragment_size, std::byte {0x5a});
		for(size_t index = 0; index < count; ++index)
		{
			const auto datagram = make_wire_datagram(
				first_message_id + index, topic, assembly_size, 0,
				fragment_payload);
			socket.send_to(asio::buffer(datagram), destination);
			if(index % 8 == 7)
				std::this_thread::sleep_for(1ms);
		}
	};
	const auto unmatched_before = receiver->statistics();
	send_incomplete_assemblies(
		unmatched_topic,
		4 * 1'024 * 1'024,
		config.max_source_reassemblies + 16,
		0x0f0000000ULL);
	RIWO_TEST_CHECK(wait_until([&]
	{
		return receiver->statistics().received_datagrams >=
			unmatched_before.received_datagrams +
			config.max_source_reassemblies + 1;
	}));
	RIWO_TEST_CHECK_EQ(receiver->statistics().reassembly_evictions,
		unmatched_before.reassembly_evictions);

	send_incomplete_assemblies(
		entry_limit_topic,
		256 * 1'024,
		config.max_source_reassemblies + 16 * RIWO_STRESS_SCALE,
		0x100000000ULL);
	send_incomplete_assemblies(
		byte_limit_topic,
		4 * 1'024 * 1'024,
		5 + 8 * RIWO_STRESS_SCALE,
		0x110000000ULL);
	RIWO_TEST_CHECK(wait_until([&]
	{
		return receiver->statistics().reassembly_evictions > 0;
	}));

	// A small malformed-datagram burst exceeds the per-source packet bucket.
	const size_t malformed_count = config.source_datagram_burst +
		4'000 * RIWO_STRESS_SCALE;
	for(size_t index = 0; index < malformed_count; ++index)
	{
		std::array<std::byte,wire_header_size> malformed {};
		write_u32(malformed.data(), wire_magic);
		malformed[4] = static_cast<std::byte>(wire_version);
		malformed[5] = static_cast<std::byte>(1);
		write_u16(malformed.data() + 6, wire_header_size);
		write_u64(malformed.data() + 8, index);
		write_u32(malformed.data() + 16, 1);
		write_u32(malformed.data() + 20, 1);
		write_u32(malformed.data() + 28, 1);
		write_u32(malformed.data() + 32, 1);
		write_u32(malformed.data() + 36, 1);
		socket.send_to(asio::buffer(malformed), destination);
	}
	RIWO_TEST_CHECK(wait_until([&]
	{
		return receiver->statistics().rate_limited_datagrams > 0;
	}));

	const auto probe_payload = std::as_bytes(std::span(&probe_value, 1));
	for(uint64_t attempt = 0; attempt < 1'000 and
		recovered.load(std::memory_order_acquire) == 0; ++attempt)
	{
		const auto datagram = make_wire_datagram(
			0x200000000ULL + attempt, probe_topic, sizeof(probe_value), 0,
			probe_payload);
		socket.send_to(asio::buffer(datagram), destination);
		std::this_thread::sleep_for(2ms);
	}
	RIWO_TEST_CHECK(recovered.load(std::memory_order_acquire) > 0);
	RIWO_TEST_CHECK(not invalid.load(std::memory_order_relaxed));
	const auto statistics = receiver->statistics();
	RIWO_TEST_CHECK(statistics.invalid_datagrams > 0);
	RIWO_TEST_CHECK(statistics.rate_limited_datagrams > 0);
	RIWO_TEST_CHECK(statistics.reassembly_evictions > 0);
	receiver->cancel();
}

} //namespace

int main(int argc, const char *const argv[])
{
	return riwo::test::run(argc, argv, {
		{"udp concurrent multicast burst and recovery",
			concurrent_multicast_burst_and_recovery},
		{"udp lifecycle churn under traffic", lifecycle_churn_under_traffic},
		{"udp slow callback queue pressure and recovery",
			slow_callback_queue_pressure_and_recovery},
		{"udp malformed and incomplete fragment storm recovery",
			malformed_and_incomplete_fragment_storm_recovery},
	});
}
