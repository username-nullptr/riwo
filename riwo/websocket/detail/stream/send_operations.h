// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_DETAIL_STREAM_SEND_OPERATIONS_H
#define RIWO_WEBSOCKET_DETAIL_STREAM_SEND_OPERATIONS_H

#include <riwo/websocket/detail/stream/frame_builder.h>
#include <riwo/websocket/protocol/detail/utf8.h>

namespace riwo::websocket::detail
{

enum class send_kind : uint8_t {
	data, application_control,
};

struct prepared_data_frame
{
	prepared_frame frame {};
	optional<message_type> next_message_type {};

	size_t next_message_size = 0;
	optional<utf8_validator> next_utf8 {};
};

struct send_operation
{
	send_kind kind = send_kind::data;
	std::vector<prepared_frame> frames {};

	std::shared_ptr<std::vector<std::byte>> payload_owner {};
	asio::any_completion_handler<void(error_code,size_t)> completion {};

	size_t frame_index = 0;
	size_t transferred = 0;
	size_t queued_payload_size = 0;

	uint64_t sequence = 0;
	uint64_t id = 0;

	bool cancel_requested = false;
	bool queued_counted = false;
	bool explicit_data_frame = false;

	optional<message_type> next_message_type {};
	size_t next_message_size = 0;

	optional<utf8_validator> next_utf8 {};
};

struct write_waiter
{
	uint64_t id = 0;
	uint64_t target = 0;

	asio::any_completion_handler<void(error_code)> completion {};
};

enum class wire_frame_kind : uint8_t
{
	data, application_control, auto_pong,
	local_close, close_response, protocol_close,
};

} //namespace riwo::websocket::detail


#endif //RIWO_WEBSOCKET_DETAIL_STREAM_SEND_OPERATIONS_H
