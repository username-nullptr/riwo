// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_UTILS_UTILS_SBUS_DETAIL_UDP_INTERFACE_H
#define RIWO_UTILS_UTILS_SBUS_DETAIL_UDP_INTERFACE_H

#include <riwo/utils/global.h>

#if RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT

namespace riwo::utils::sbus
{

class RIWO_UTILS_API udp_interface final :
	public std::enable_shared_from_this<udp_interface>
{
	RIWO_DISABLE_COPY_MOVE(udp_interface)

public:
	enum class msg_range {
		process, lan, internet
	};
	struct config_t
	{
		msg_range send_range = msg_range::process;
		msg_range recv_range = msg_range::process;

		std::array<unsigned char,4> multicast_group {239,255,71,83};
		uint16_t multicast_port = 57'183;

		size_t max_topic_size = 4 * 1'024;
		size_t max_payload_size = 16 * 1'024 * 1'024;

		size_t socket_send_buffer_size = 4 * 1'024 * 1'024;
		size_t socket_receive_buffer_size = 4 * 1'024 * 1'024;

		size_t global_datagram_rate = 50'000;
		size_t global_datagram_burst = 16'384;
		size_t global_byte_rate = 256 * 1'024 * 1'024;
		size_t global_byte_burst = 128 * 1'024 * 1'024;

		size_t source_datagram_rate = 10'000;
		size_t source_datagram_burst = 4'096;
		size_t source_byte_rate = 64 * 1'024 * 1'024;
		size_t source_byte_burst = 32 * 1'024 * 1'024;

		size_t max_tracked_sources = 1'024;
		size_t source_prune_datagram_interval = 1'024;

		std::chrono::milliseconds source_state_timeout =
			std::chrono::minutes(1);

		size_t max_reassemblies = 128;
		size_t max_reassembly_bytes = 64 * 1'024 * 1'024;
		size_t max_source_reassemblies = 8;
		size_t max_source_reassembly_bytes = 16 * 1'024 * 1'024;

		std::chrono::milliseconds reassembly_timeout =
			std::chrono::seconds(5);

		std::chrono::milliseconds reassembly_check_interval =
			std::chrono::seconds(1);

		size_t delivery_queue_capacity = 256;
		size_t delivery_queue_bytes = 32 * 1'024 * 1'024;

		[[nodiscard]] bool operator==(const config_t&) const noexcept = default;
	};

	struct receive_statistics
	{
		uint64_t received_datagrams = 0;
		uint64_t invalid_datagrams = 0;

		uint64_t rate_limited_datagrams = 0;
		uint64_t reassembly_evictions = 0;

		uint64_t delivery_queue_drops = 0;
		uint64_t delivered_messages = 0;
	};

public:
	explicit udp_interface(const config_t &config);
	udp_interface();
	~udp_interface();

	static void set_config(config_t config);
	static config_t config() noexcept;

public:
	[[nodiscard]] receive_statistics statistics() const noexcept;
	static void publish(std::string_view topic, const void *buffer, size_t size);

	uint64_t subscribe(std::string_view topic, std::function<void(const void*, size_t)> func);
	uint64_t subscribe(std::function<void(std::string_view topic, const void*, size_t)> func);

	void cancel_topic(std::string_view topic);
	void cancel_sid(uint64_t sid);
	void cancel();

private:
	class impl;
	std::shared_ptr<impl> m_impl {};
};

} //namespace riwo::utils::sbus

#endif //RIWO_UTILS_SBUS_UDP_INTERFACE_SUPPORT
#endif //RIWO_UTILS_UTILS_SBUS_DETAIL_UDP_INTERFACE_H
