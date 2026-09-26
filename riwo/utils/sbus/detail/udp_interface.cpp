// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "udp_interface.h"
#if RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT

#include <riwo/core/shared_mutex.h>
#include <riwo/utils/logger.h>

namespace riwo::utils::sbus { namespace
{

using udp = asio::ip::udp;
using clock_t = std::chrono::steady_clock;

constexpr uint32_t g_magic = 0x4C475342; // "LGSB"
constexpr uint8_t g_version = 2;

constexpr size_t g_header_size = 48;
constexpr size_t g_datagram_size = 60 * 1'024;

// config_t is fixed-size and copied without allocation or system calls.  Its
// critical section is short enough to justify the low-latency spin policy.
spin_shared_mutex g_config_mutex {};
udp_interface::config_t g_config {};

int g_process_anchor = 0;

[[nodiscard]] bool valid_range(udp_interface::msg_range value) noexcept
{
	using msg_range = udp_interface::msg_range;
	return value == msg_range::process or
		value == msg_range::lan or value == msg_range::internet;
}

[[nodiscard]] uint8_t range_value(udp_interface::msg_range value) noexcept {
	return static_cast<uint8_t>(value);
}

[[nodiscard]] int multicast_hops(udp_interface::msg_range value) noexcept
{
	switch(value)
	{
		case udp_interface::msg_range::process:  return 0;
		case udp_interface::msg_range::lan:      return 1;
		case udp_interface::msg_range::internet: return 64;
	}
	return 0;
}

[[nodiscard]] udp_interface::config_t validate_config(const udp_interface::config_t &config)
{
	constexpr auto max_socket_buffer = static_cast<size_t>(
		std::numeric_limits<int>::max()
	);
	if( not valid_range(config.send_range) or
		not valid_range(config.recv_range) or
		not asio::ip::address_v4(config.multicast_group).is_multicast() or
		config.multicast_port == 0 or
		config.max_topic_size > std::numeric_limits<uint16_t>::max() or
		config.max_topic_size >= g_datagram_size - g_header_size or
		config.max_payload_size > std::numeric_limits<uint32_t>::max() or
		config.socket_send_buffer_size == 0 or
		config.socket_send_buffer_size > max_socket_buffer or
		config.socket_receive_buffer_size == 0 or
		config.socket_receive_buffer_size > max_socket_buffer or
		config.source_prune_datagram_interval == 0 or
		config.source_state_timeout < std::chrono::milliseconds::zero() or
		config.reassembly_timeout < std::chrono::milliseconds::zero() or
		config.reassembly_check_interval <= std::chrono::milliseconds::zero() )
	{
		invalid_argument::loc_throw (
			"riwo::utils::sbus::udp_interface: invalid configuration"
		);
	}
	return config;
}

[[nodiscard]] uint64_t process_token() noexcept
{
	static const uint64_t value = []
	{
		uint64_t token = static_cast<uint64_t>(
			clock_t::now().time_since_epoch().count()
		);
		token ^= reinterpret_cast<uintptr_t>(&g_process_anchor);
		token ^= std::hash<std::thread::id>{}(std::this_thread::get_id());

		token = (token ^ (token >> 30)) * 0xbf58476d1ce4e5b9ULL;
		token = (token ^ (token >> 27)) * 0x94d049bb133111ebULL;

		return token ^ (token >> 31);
	}();
	return value;
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

[[nodiscard]] uint16_t read_u16(const std::byte *input) noexcept
{
	return static_cast<uint16_t>(
		(std::to_integer<uint16_t>(input[0]) << 8) |
		 std::to_integer<uint16_t>(input[1])
	);
}

[[nodiscard]] uint32_t read_u32(const std::byte *input) noexcept
{
	return (std::to_integer<uint32_t>(input[0]) << 24) |
		   (std::to_integer<uint32_t>(input[1]) << 16) |
		   (std::to_integer<uint32_t>(input[2]) << 8) |
		   std::to_integer<uint32_t>(input[3]);
}

[[nodiscard]] uint64_t read_u64(const std::byte *input) noexcept {
	return (static_cast<uint64_t>(read_u32(input)) << 32) | read_u32(input + 4);
}

[[nodiscard]] size_t fragment_capacity(size_t topic_size) noexcept {
	return g_datagram_size - g_header_size - topic_size;
}

[[nodiscard]] uint32_t fragment_count(size_t payload_size, size_t capacity) noexcept
{
	if( payload_size == 0 )
		return 1;
	return static_cast<uint32_t>((payload_size + capacity - 1) / capacity);
}

[[noreturn]] void uncaught_exception(const std::exception &exception) noexcept
{
	riwo_utils_clog_critical("Riwo.Utils", "Uncaught exception: {}", exception);
	forced_termination();
}

class RIWO_DECL_HIDDEN udp_sender
{
	RIWO_DISABLE_COPY_MOVE(udp_sender)

public:
	udp_sender() : m_socket(m_context)
	{
		m_socket.open(udp::v4());
		m_socket.set_option(asio::ip::multicast::enable_loopback(true));
	}

	void publish(const udp_interface::config_t &config,
		std::string_view topic, const void *buffer, size_t size)
	{
		if( size > 0 and buffer == nullptr )
			invalid_argument::loc_throw("riwo::utils::sbus::udp_interface::publish: null buffer");

		if( size > config.max_payload_size )
			length_error::loc_throw("riwo::utils::sbus::udp_interface::publish: payload too large");

		if( topic.size() > config.max_topic_size )
			length_error::loc_throw("riwo::utils::sbus::udp_interface::publish: topic too long");

		const auto capacity = fragment_capacity(topic.size());
		const auto count = fragment_count(size, capacity);

		const auto message_id = m_message_sequence.fetch_add(1, std::memory_order_relaxed);
		const auto packet_range = config.send_range;

		const auto publisher_token = process_token();
		const auto *payload = static_cast<const std::byte*>(buffer);

		std::lock_guard lock(m_mutex);
		m_destination = udp::endpoint (
			asio::ip::address_v4(config.multicast_group), config.multicast_port
		);
		if( packet_range != m_socket_range )
		{
			m_socket.set_option(asio::ip::multicast::hops (
				multicast_hops(packet_range)
			));
			m_socket_range = packet_range;
		}
		if( config.socket_send_buffer_size != m_socket_buffer_size )
		{
			error_code ignored; RIWO_UNUSED(ignored);
			ignored = m_socket.set_option(asio::socket_base::send_buffer_size(
				static_cast<int>(config.socket_send_buffer_size)), ignored
			);
			m_socket_buffer_size = config.socket_send_buffer_size;
		}
		std::vector<std::byte> datagram;
		datagram.reserve(g_datagram_size);

		for(uint32_t index = 0; index < count; ++index)
		{
			const size_t offset = static_cast<size_t>(index) * capacity;
			const size_t part_size = size == 0 ? 0 : std::min(capacity, size - offset);
			datagram.resize(g_header_size + topic.size() + part_size);

			auto *header = datagram.data();
			write_u32(header, g_magic);

			header[4] = static_cast<std::byte>(g_version);
			header[5] = static_cast<std::byte>(range_value(packet_range));

			write_u16(header + 6, g_header_size);
			write_u64(header + 8, message_id);

			write_u32(header + 16, index);
			write_u32(header + 20, count);

			write_u16(header + 24, static_cast<uint16_t>(topic.size()));
			write_u16(header + 26, 0);

			write_u32(header + 28, static_cast<uint32_t>(size));
			write_u32(header + 32, static_cast<uint32_t>(offset));

			write_u32(header + 36, static_cast<uint32_t>(part_size));
			write_u64(header + 40, publisher_token);

			std::ranges::copy (
				topic, reinterpret_cast<char*>(datagram.data() + g_header_size)
			);
			if( part_size > 0 )
			{
				std::copy_n(payload + offset, part_size,
					datagram.data() + g_header_size + topic.size()
				);
			}
			m_socket.send_to(asio::buffer(datagram), m_destination);
		}
	}

private:
	asio::io_context m_context {};
	udp::socket m_socket;
	udp::endpoint m_destination {};

	std::mutex m_mutex {};
	udp_interface::msg_range m_socket_range =
		static_cast<udp_interface::msg_range>(std::numeric_limits<uint8_t>::max());

	size_t m_socket_buffer_size = 0;
	std::atomic_uint64_t m_message_sequence {
		static_cast<uint64_t>(clock_t::now().time_since_epoch().count())
	};
};

[[nodiscard]] udp_sender &sender()
{
	static udp_sender value;
	return value;
}

struct message_key
{
	uint32_t address = 0;
	uint16_t port = 0;
	uint64_t message_id = 0;

	[[nodiscard]] bool operator==
	(const message_key&) const noexcept = default;
};

struct message_key_hash
{
	[[nodiscard]] size_t operator()(const message_key &key) const noexcept
	{
		auto value = std::hash<uint64_t>{}(key.message_id);

		value ^= std::hash<uint32_t>{}(key.address) +
			0x9e3779b9U + (value << 6) + (value >> 2);

		value ^= std::hash<uint16_t>{}(key.port) +
			0x9e3779b9U + (value << 6) + (value >> 2);

		return value;
	}
};

struct udp_string_hash
{
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
};

class udp_payload_buffer
{
public:
	udp_payload_buffer() = default;

	explicit udp_payload_buffer(size_t size) {
		resize(size);
	}

	void resize(size_t size)
	{
		m_size = size;
		m_storage.resize((size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
	}

	[[nodiscard]] std::byte *data() noexcept {
		return reinterpret_cast<std::byte*>(m_storage.data());
	}

	[[nodiscard]] const std::byte *data() const noexcept {
		return reinterpret_cast<const std::byte*>(m_storage.data());
	}

	[[nodiscard]] size_t size() const noexcept {
		return m_size;
	}

private:
	std::vector<std::max_align_t> m_storage {};
	size_t m_size = 0;
};

class token_bucket
{
public:
	token_bucket(size_t rate, size_t capacity, clock_t::time_point now) :
		m_rate(static_cast<double>(rate)),
		m_capacity(static_cast<double>(capacity)),
		m_tokens(m_capacity),
		m_updated(now) {}

	void refill(clock_t::time_point now) noexcept
	{
		const auto elapsed = std::chrono::duration<double>(now - m_updated).count();
		if( elapsed <= 0 )
			return ;

		m_tokens = std::min(m_capacity, m_tokens + elapsed * m_rate);
		m_updated = now;
	}

	[[nodiscard]] bool available(size_t amount) const noexcept {
		return m_tokens >= static_cast<double>(amount);
	}

	void consume(size_t amount) noexcept {
		m_tokens -= static_cast<double>(amount);
	}

private:
	double m_rate = 0.0;
	double m_capacity = 0.0;
	double m_tokens = 0.0;
	clock_t::time_point m_updated {};
};

} //namespace

class RIWO_DECL_HIDDEN udp_interface::impl final :
	public std::enable_shared_from_this<impl>
{
	RIWO_DISABLE_COPY_MOVE(impl)

	using topic_callback_t = std::function<void(const void*,size_t)>;
	using global_callback_t = std::function<void(std::string_view,const void*,size_t)>;
	using topic_subscribers_t = std::unordered_map<uint64_t,topic_callback_t>;

	using topic_map_t = std::unordered_map <
		std::string, topic_subscribers_t, udp_string_hash, std::equal_to<>
	>;

	struct assembly
	{
		std::string topic {};
		udp_payload_buffer payload {};
		std::vector<uint8_t> received {};

		uint32_t fragment_count = 0;
		uint32_t received_count = 0;

		msg_range packet_range = msg_range::process;
		uint64_t publisher_token = 0;
		clock_t::time_point touched {};
	};

	struct source_state
	{
		explicit source_state(const config_t &config, clock_t::time_point now) :
			datagrams(config.source_datagram_rate, config.source_datagram_burst, now),
			bytes(config.source_byte_rate, config.source_byte_burst, now),
			touched(now) {}

		token_bucket datagrams;
		token_bucket bytes;
		clock_t::time_point touched {};
	};

	struct delivery_event
	{
		std::string topic {};
		udp_payload_buffer payload {};
	};

public:
	[[nodiscard]] static std::shared_ptr<impl> create(const config_t &config)
	{
		auto value = std::shared_ptr<impl>(new impl(config));
		value->start();
		return value;
	}

	~impl() {
		stop();
	}

	uint64_t subscribe(std::string_view topic, topic_callback_t callback)
	{
		std::unique_lock lock(m_subscribers_mutex);
		const auto sid = m_sid_sequence++;

		m_topic_subscribers[std::string(topic)].emplace(sid, std::move(callback));
		m_topics_by_sid.emplace(sid, topic);
		return sid;
	}

	uint64_t subscribe(global_callback_t callback)
	{
		std::unique_lock lock(m_subscribers_mutex);
		const auto sid = m_sid_sequence++;

		m_global_subscribers.emplace(sid, std::move(callback));
		return sid;
	}

	void cancel_topic(std::string_view topic)
	{
		std::unique_lock lock(m_subscribers_mutex);
		if( auto position = m_topic_subscribers.find(topic);
			position != m_topic_subscribers.end() )
		{
			for(const auto &[sid, callback] : position->second)
			{
				ignore_unused(callback);
				m_topics_by_sid.erase(sid);
			}
			m_topic_subscribers.erase(position);
		}
	}

	void cancel_sid(uint64_t sid)
	{
		std::unique_lock lock(m_subscribers_mutex);
		if( m_global_subscribers.erase(sid) > 0 )
			return ;

		const auto topic_position = m_topics_by_sid.find(sid);
		if( topic_position == m_topics_by_sid.end() )
			return ;

		if( auto position = m_topic_subscribers.find(topic_position->second);
			position != m_topic_subscribers.end() )
		{
			position->second.erase(sid);
			if( position->second.empty() )
				m_topic_subscribers.erase(position);
		}
		m_topics_by_sid.erase(topic_position);
	}

	void cancel()
	{
		std::unique_lock lock(m_subscribers_mutex);
		m_topic_subscribers.clear();
		m_topics_by_sid.clear();
		m_global_subscribers.clear();
	}

	[[nodiscard]] receive_statistics statistics() const noexcept
	{
		return {
			.received_datagrams = m_received_datagrams.load(std::memory_order_relaxed),
			.invalid_datagrams = m_invalid_datagrams.load(std::memory_order_relaxed),

			.rate_limited_datagrams = m_rate_limited_datagrams.load(std::memory_order_relaxed),
			.reassembly_evictions = m_reassembly_evictions.load(std::memory_order_relaxed),

			.delivery_queue_drops = m_delivery_queue_drops.load(std::memory_order_relaxed),
			.delivered_messages = m_delivered_messages.load(std::memory_order_relaxed),
		};
	}

	void stop() noexcept
	{
		if( m_stopped.exchange(true, std::memory_order_acq_rel) )
			return ;

		error_code error; RIWO_UNUSED(error);
		error = m_socket.cancel(error);
		error = m_socket.close(error);
		try {
			ignore_unused(m_reassembly_timer.cancel());
		}
		catch(...) {}
		m_context.stop();
		m_delivery_condition.notify_all();

		if( m_thread.joinable() )
		{
			if( m_thread.get_id() == std::this_thread::get_id() )
				m_thread.detach();
			else
				m_thread.join();
		}
		if( m_delivery_thread.joinable() )
		{
			if( m_delivery_thread.get_id() == std::this_thread::get_id() )
				m_delivery_thread.detach();
			else
				m_delivery_thread.join();
		}
	}

private:
	explicit impl(const config_t &config) :
		m_config(config),
		m_socket(m_context),
		m_reassembly_timer(m_context),
		m_global_datagrams(m_config.global_datagram_rate, m_config.global_datagram_burst, clock_t::now()),
		m_global_bytes(m_config.global_byte_rate, m_config.global_byte_burst, clock_t::now())
	{
		m_socket.open(udp::v4());
		m_socket.set_option(asio::socket_base::reuse_address(true));

		error_code ignored; RIWO_UNUSED(ignored);
		ignored = m_socket.set_option(
			asio::socket_base::receive_buffer_size(
				static_cast<int>(m_config.socket_receive_buffer_size)), ignored
		);
		m_socket.bind(udp::endpoint(udp::v4(), m_config.multicast_port));

		m_socket.set_option(asio::ip::multicast::join_group (
			asio::ip::address_v4(m_config.multicast_group)
		));
	}

	void start()
	{
		start_receive();
		start_reassembly_timer();

		auto receive_self = shared_from_this();
		auto delivery_self = receive_self;
		try {
			m_delivery_thread = std::thread(
			[self = std::move(delivery_self)]() noexcept
			{
				try {
					self->delivery_loop();
				}
				catch(const std::exception &exception) {
					uncaught_exception(exception);
				}
				catch(...) {
					forced_termination();
				}
			});
			m_thread = std::thread([self = std::move(receive_self)]() noexcept
			{
				try {
					self->m_context.run();
				}
				catch(const std::exception &exception) {
					uncaught_exception(exception);
				}
				catch(...) {
					forced_termination();
				}
			});
		}
		catch(...)
		{
			stop();
			throw;
		}
	}

	void start_receive()
	{
		if( m_stopped.load(std::memory_order_acquire) )
			return ;

		m_socket.async_receive_from(asio::buffer(m_receive_buffer), m_remote_endpoint,
		[this](error_code error, size_t size)
		{
			if( not error )
				receive(size);

			if( not m_stopped.load(std::memory_order_acquire) )
				start_receive();
		});
	}

	void start_reassembly_timer()
	{
		if( m_stopped.load(std::memory_order_acquire) )
			return ;

		m_reassembly_timer.expires_after(m_config.reassembly_check_interval);
		m_reassembly_timer.async_wait([this](error_code error)
		{
			if( not error )
				prune_reassemblies();

			if( not error and not m_stopped.load(std::memory_order_acquire) )
				start_reassembly_timer();
		});
	}

	[[nodiscard]] bool allow_receive(uint32_t address, size_t size)
	{
		const auto now = clock_t::now();
		m_global_datagrams.refill(now);
		m_global_bytes.refill(now);

		if( ++m_source_prune_sequence % m_config.source_prune_datagram_interval == 0 )
		{
			const auto oldest = now - m_config.source_state_timeout;
			for(auto source_position = m_sources.begin();
				source_position != m_sources.end(); )
			{
				if( source_position->second.touched < oldest )
					source_position = m_sources.erase(source_position);
				else
					++source_position;
			}
		}
		auto position = m_sources.find(address);
		if( position == m_sources.end() )
		{
			if( m_sources.size() >= m_config.max_tracked_sources )
				return false;
			position = m_sources.emplace(address, source_state(m_config, now)).first;
		}
		auto &source = position->second;
		source.datagrams.refill(now);
		source.bytes.refill(now);
		source.touched = now;

		if( not m_global_datagrams.available(1) or
			not m_global_bytes.available(size) or
			not source.datagrams.available(1) or
			not source.bytes.available(size) )
			return false;

		m_global_datagrams.consume(1);
		m_global_bytes.consume(size);

		source.datagrams.consume(1);
		source.bytes.consume(size);
		return true;
	}

	[[nodiscard]] bool has_subscribers(std::string_view topic)
	{
		std::shared_lock lock(m_subscribers_mutex);
		return not m_global_subscribers.empty() or m_topic_subscribers.contains(topic);
	}

	void enqueue_delivery(std::string topic, udp_payload_buffer payload)
	{
		{
			const auto size = payload.size();
			std::lock_guard lock(m_delivery_mutex);

			if( m_stopped.load(std::memory_order_acquire) )
				return ;

			if( m_delivery_queue.size() >= m_config.delivery_queue_capacity or
				size > m_config.delivery_queue_bytes or
				m_delivery_bytes > m_config.delivery_queue_bytes - size )
			{
				m_delivery_queue_drops.fetch_add(1, std::memory_order_relaxed);
				return ;
			}
			m_delivery_bytes += size;

			m_delivery_queue.push_back ({
				.topic = std::move(topic),
				.payload = std::move(payload)
			});
		}
		m_delivery_condition.notify_one();
	}

	void delivery_loop()
	{
		for(;;)
		{
			delivery_event event;
			{
				std::unique_lock lock(m_delivery_mutex);
				m_delivery_condition.wait(lock, [this]
				{
					return m_stopped.load(std::memory_order_acquire) or
						not m_delivery_queue.empty();
				});
				if( m_stopped.load(std::memory_order_acquire) )
				{
					m_delivery_queue.clear();
					m_delivery_bytes = 0;
					return ;
				}
				event = std::move(m_delivery_queue.front());
				m_delivery_queue.pop_front();
				m_delivery_bytes -= event.payload.size();
			}
			deliver(event.topic, event.payload.data(), event.payload.size());
			m_delivered_messages.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void receive(size_t size)
	{
		m_received_datagrams.fetch_add(1, std::memory_order_relaxed);
		if( not m_remote_endpoint.address().is_v4() )
		{
			m_invalid_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		const auto source_address = m_remote_endpoint.address().to_v4().to_uint();
		if( not allow_receive(source_address, size) )
		{
			m_rate_limited_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		if( size < g_header_size )
		{
			m_invalid_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		const auto *header = m_receive_buffer.data();
		if( read_u32(header) != g_magic or
			std::to_integer<uint8_t>(header[4]) != g_version or
			read_u16(header + 6) != g_header_size )
		{
			m_invalid_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		const auto message_id = read_u64(header + 8);
		const auto packet_range = static_cast<msg_range>(
			std::to_integer<uint8_t>(header[5])
		);
		const auto index = read_u32(header + 16);
		const auto count = read_u32(header + 20);

		const auto topic_size = read_u16(header + 24);
		const auto total_size = read_u32(header + 28);

		const auto offset = read_u32(header + 32);
		const auto part_size = read_u32(header + 36);
		const auto publisher_token = read_u64(header + 40);

		if( not valid_range(packet_range) or
			range_value(packet_range) > range_value(m_config.recv_range) or
			(packet_range == msg_range::process and publisher_token != process_token()) or
			topic_size > m_config.max_topic_size or
			total_size > m_config.max_payload_size )
		{
			m_invalid_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		const auto capacity = fragment_capacity(topic_size);

		if( const auto expected_count = fragment_count(total_size, capacity);
			count != expected_count or index >= count or offset != static_cast<uint64_t>(index) * capacity )
		{
			m_invalid_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		const size_t expected_size = total_size == 0 ?
			0 : std::min(capacity, static_cast<size_t>(total_size) - offset);

		if( part_size != expected_size or size != g_header_size + topic_size + part_size )
		{
			m_invalid_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		const auto *topic_data = reinterpret_cast<const char*>(
			m_receive_buffer.data() + g_header_size
		);
		const auto *payload_data = m_receive_buffer.data() + g_header_size + topic_size;
		const std::string_view topic(topic_data, topic_size);

		if( not has_subscribers(topic) )
			return ;

		if( count == 1 )
		{
			udp_payload_buffer payload(part_size);
			if( part_size > 0 )
				std::copy_n(payload_data, part_size, payload.data());

			enqueue_delivery(std::string(topic), std::move(payload));
			return ;
		}
		prune_reassemblies();

		const message_key key {
			.address = source_address,
			.port = m_remote_endpoint.port(),
			.message_id = message_id
		};
		auto position = m_reassemblies.find(key);
		if( position == m_reassemblies.end() )
		{
			if( not make_reassembly_room(source_address, total_size) )
			{
				m_reassembly_evictions.fetch_add(1, std::memory_order_relaxed);
				return ;
			}
			assembly value;

			value.topic.assign(topic);
			value.payload.resize(total_size);
			value.received.resize(count);

			value.fragment_count = count;
			value.packet_range = packet_range;
			value.publisher_token = publisher_token;

			value.touched = clock_t::now();
			m_reassembly_bytes += total_size;

			position = m_reassemblies.emplace(key, std::move(value)).first;
		}
		auto &value = position->second;

		if( value.fragment_count != count or value.payload.size() != total_size or
			value.packet_range != packet_range or
			value.publisher_token != publisher_token or
			value.topic != topic )
		{
			erase_reassembly(position);
			m_invalid_datagrams.fetch_add(1, std::memory_order_relaxed);
			return ;
		}
		value.touched = clock_t::now();
		if( value.received[index] )
			return ;

		std::copy_n(payload_data, part_size, value.payload.data() + offset);
		value.received[index] = 1;

		++value.received_count;
		if( value.received_count != value.fragment_count )
			return ;

		auto complete_topic = std::move(value.topic);
		auto complete_payload = std::move(value.payload);

		m_reassembly_bytes -= complete_payload.size();
		m_reassemblies.erase(position);

		enqueue_delivery(std::move(complete_topic), std::move(complete_payload));
	}

	void deliver(std::string_view topic, const void *data, size_t size)
	{
		std::vector<global_callback_t> global_callbacks;
		std::vector<topic_callback_t> topic_callbacks;
		{
			std::shared_lock lock(m_subscribers_mutex);
			global_callbacks.reserve(m_global_subscribers.size());

			for(const auto &[sid, callback] : m_global_subscribers)
			{
				ignore_unused(sid);
				global_callbacks.emplace_back(callback);
			}
			if( const auto position = m_topic_subscribers.find(topic);
				position != m_topic_subscribers.end() )
			{
				topic_callbacks.reserve(position->second.size());
				for(const auto &[sid, callback] : position->second)
				{
					ignore_unused(sid);
					topic_callbacks.emplace_back(callback);
				}
			}
		}
		for(auto &callback : global_callbacks)
			callback(topic, data, size);

		for(auto &callback : topic_callbacks)
			callback(data, size);
	}

	void prune_reassemblies()
	{
		const auto oldest = clock_t::now() - m_config.reassembly_timeout;
		for(auto position = m_reassemblies.begin(); position != m_reassemblies.end(); )
		{
			if( position->second.touched < oldest )
			{
				m_reassembly_bytes -= position->second.payload.size();
				position = m_reassemblies.erase(position);
				m_reassembly_evictions.fetch_add(1, std::memory_order_relaxed);
			}
			else
				++position;
		}
	}

	[[nodiscard]] bool make_reassembly_room(uint32_t address, size_t size)
	{
		if( size > m_config.max_source_reassembly_bytes or
			size > m_config.max_reassembly_bytes )
			return false;

		for(;;)
		{
			size_t source_count = 0;
			size_t source_bytes = 0;
			auto oldest = m_reassemblies.end();

			for(auto position = m_reassemblies.begin();
				position != m_reassemblies.end(); ++position)
			{
				if( position->first.address != address )
					continue ;

				++source_count;
				source_bytes += position->second.payload.size();

				if( oldest == m_reassemblies.end() or
					position->second.touched < oldest->second.touched )
					oldest = position;
			}
			if( source_count < m_config.max_source_reassemblies and
				source_bytes <= m_config.max_source_reassembly_bytes - size )
				break ;

			if( oldest == m_reassemblies.end() )
				return false;

			erase_reassembly(oldest);
			m_reassembly_evictions.fetch_add(1, std::memory_order_relaxed);
		}

		while( not m_reassemblies.empty() and
			   (m_reassemblies.size() >= m_config.max_reassemblies or
			   	m_reassembly_bytes + size > m_config.max_reassembly_bytes) )
		{
			auto oldest = std::ranges::min_element(m_reassemblies,
				[](const auto &left, const auto &right) {
					return left.second.touched < right.second.touched;
				});
			erase_reassembly(oldest);
			m_reassembly_evictions.fetch_add(1, std::memory_order_relaxed);
		}
		return m_reassemblies.size() < m_config.max_reassemblies and
			m_reassembly_bytes <= m_config.max_reassembly_bytes - size;
	}

	void erase_reassembly(std::unordered_map<message_key,assembly,message_key_hash>::iterator position)
	{
		m_reassembly_bytes -= position->second.payload.size();
		m_reassemblies.erase(position);
	}

private:
	const config_t m_config;
	asio::io_context m_context {};

	udp::socket m_socket;
	asio::steady_timer m_reassembly_timer;
	udp::endpoint m_remote_endpoint {};

	std::array<std::byte,g_datagram_size> m_receive_buffer {};
	std::thread m_thread {};
	std::thread m_delivery_thread {};

	std::atomic_bool m_stopped {false};
	// Delivery copies callback vectors and subscription updates allocate maps.
	shared_mutex m_subscribers_mutex {};

	std::mutex m_delivery_mutex {};
	std::condition_variable m_delivery_condition {};

	uint64_t m_sid_sequence = 0;
	topic_map_t m_topic_subscribers {};

	std::unordered_map<uint64_t,std::string> m_topics_by_sid {};
	std::unordered_map<uint64_t,global_callback_t> m_global_subscribers {};

	std::deque<delivery_event> m_delivery_queue {};
	size_t m_delivery_bytes = 0;

	std::unordered_map<message_key,assembly,message_key_hash> m_reassemblies {};
	size_t m_reassembly_bytes = 0;

	token_bucket m_global_datagrams;
	token_bucket m_global_bytes;

	std::unordered_map<uint32_t,source_state> m_sources {};
	size_t m_source_prune_sequence = 0;

	std::atomic_uint64_t m_received_datagrams {0};
	std::atomic_uint64_t m_invalid_datagrams {0};

	std::atomic_uint64_t m_rate_limited_datagrams {0};
	std::atomic_uint64_t m_reassembly_evictions {0};

	std::atomic_uint64_t m_delivery_queue_drops {0};
	std::atomic_uint64_t m_delivered_messages {0};
};

udp_interface::udp_interface(const config_t &config) :
	m_impl(impl::create(validate_config(config)))
{

}

udp_interface::udp_interface() :
	udp_interface(config())
{

}

udp_interface::~udp_interface()
{
	m_impl->stop();
}

void udp_interface::set_config(config_t config)
{
	config = validate_config(config);
	spin_shared_unique_lock lock(g_config_mutex);
	g_config = config;
}

udp_interface::config_t udp_interface::config() noexcept
{
	spin_shared_lock lock(g_config_mutex);
	return g_config;
}

udp_interface::receive_statistics udp_interface::statistics() const noexcept
{
	return m_impl->statistics();
}

void udp_interface::publish(std::string_view topic, const void *buffer, size_t size)
{
	sender().publish(config(), topic, buffer, size);
}

uint64_t udp_interface::subscribe(std::string_view topic, std::function<void(const void*,size_t)> func)
{
	return m_impl->subscribe(topic, std::move(func));
}

uint64_t udp_interface::subscribe(std::function<void(std::string_view,const void*,size_t)> func)
{
	return m_impl->subscribe(std::move(func));
}

void udp_interface::cancel_topic(std::string_view topic)
{
	m_impl->cancel_topic(topic);
}

void udp_interface::cancel_sid(uint64_t sid)
{
	m_impl->cancel_sid(sid);
}

void udp_interface::cancel()
{
	m_impl->cancel();
}

} //namespace riwo::utils::sbus

#endif  //RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
