// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_EVENT_H
#define RIWO_EXP_HTTP_PROTOCOL_EVENT_H

#include <riwo/experimental/http/protocol/body_spec.h>
#include <riwo/experimental/http/protocol/request_head.h>
#include <riwo/experimental/http/protocol/response_head.h>

#include <riwo/experimental/http/error.h>
#include <variant>

namespace riwo::experimental::http
{

using stream_token_t = uint64_t;

struct request_head_event
{
	stream_token_t stream = 0;
	request_head head {};

	body_spec body = body_spec::empty();
	protocol version = protocol::v1_1;
};

struct response_head_event
{
	stream_token_t stream = 0;
	response_head head {};

	body_spec body = body_spec::empty();
	protocol version = protocol::v1_1;
};

struct body_data_event
{
	stream_token_t stream = 0;
	const_buffer data {};
};

struct trailers_event
{
	stream_token_t stream = 0;
	fields trailers {};
};

struct stream_end_event
{
	stream_token_t stream = 0;
};

struct stream_reset_event
{
	stream_token_t stream = 0;
	error_info error {};
};

enum class connection_event_kind {
	settings, goaway, input_finished, closed
};

struct connection_event
{
	connection_event_kind kind = connection_event_kind::settings;
	optional<error_info> error {};
	optional<stream_token_t> last_stream {};
};

using protocol_event_t = std::variant <
	request_head_event,
	response_head_event,
	body_data_event,
	trailers_event,
	stream_end_event,
	stream_reset_event,
	connection_event
>;

struct open_request_action
{
	stream_token_t stream = 0;
	request_head head {};
	body_spec body = body_spec::empty();
};

struct send_response_action
{
	stream_token_t stream = 0;
	response_head head {};
	body_spec body = body_spec::empty();
};

struct send_data_action
{
	stream_token_t stream = 0;
	std::vector<std::byte> data {};
	bool end_stream = false;
};

struct send_trailers_action
{
	stream_token_t stream = 0;
	fields trailers {};
};

struct reset_stream_action
{
	stream_token_t stream = 0;
	uint32_t wire_error_code = 0;
};

struct shutdown_action
{
	uint32_t wire_error_code = 0;
	optional<stream_token_t> last_stream {};
};

using protocol_action_t = std::variant <
	open_request_action,
	send_response_action,
	send_data_action,
	send_trailers_action,
	reset_stream_action,
	shutdown_action
>;

enum class event_flow
{
	continue_, pause,
	Continue = continue_,
	Pause = pause
};

using protocol_event_handler_t = std::function <
	event_flow(const protocol_event_t&)
>;

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_EVENT_H
