// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_TYPES_H
#define RIWO_WEBSOCKET_TYPES_H

#include <riwo/websocket/protocol/types.h>
#include <riwo/websocket/ctrl_payload.h>
#include <riwo/websocket/error.h>

namespace riwo::websocket
{

enum class message_type : uint8_t {
	text = 0x1, binary = 0x2,
};

enum class connection_state : uint8_t {
	idle, open, closing, closed, failed,
};

struct close_info
{
	optional<uint16_t> code {};
	std::string reason {};
	bool clean = false;
};

template <concepts::buffer Buffer>
struct basic_message
{
	message_type type = message_type::binary;
	Buffer body {};
};

using message = basic_message<std::vector<std::byte>>;

template <concepts::buffer Buffer>
struct basic_data_frame
{
	message_type type = message_type::binary;
	Buffer body {};

	bool continuation = false;
	bool fin = true;
};

using data_frame = basic_data_frame<std::vector<std::byte>>;

using closed_callback = std::function<void(const close_info&)>;

struct message_chunk
{
	message_type type = message_type::binary;
	const_buffer body {};

	size_t offset = 0;
	bool first = false;
	bool last = false;
};

struct message_info
{
	message_type type = message_type::binary;
	size_t size = 0;
};

struct adopt_options
{
	role stream_role = role::client;
	std::vector<std::byte> pending_data {};
	std::string negotiated_subprotocol {};
	std::vector<extension> negotiated_extensions {};
};

enum class compression_mode : uint8_t {
	automatic, enabled, disabled,
};

struct write_options
{
	compression_mode compression = compression_mode::automatic;
};

struct compression_config
{
	size_t min_message_size = 0;
	int level = -1;
	bool compress_text = true;
	bool compress_binary = true;
};

struct stream_config
{
	size_t max_frame_size = 16 * 1024 * 1024;
	size_t max_message_size = 16 * 1024 * 1024;

	size_t max_queued_write_bytes = 64 * 1024 * 1024;
	size_t max_queued_write_operations = 64;

	size_t read_buffer_size = 16 * 1024;
	size_t write_fragment_size = 16 * 1024;

	std::chrono::milliseconds ping_interval {5000};
	size_t pong_timeout_retries = 0;

	std::chrono::milliseconds close_timeout {5000};
	compression_config compression {};
};

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_TYPES_H
