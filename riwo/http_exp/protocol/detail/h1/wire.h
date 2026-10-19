// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_H1_WIRE_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_H1_WIRE_H

#include <riwo/http_exp/body/spec.h>
#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/error.h>
#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/protocol/response_head.h>

namespace riwo::http_exp::detail::h1
{

enum class body_framing { none, content_length, chunked, until_eof };

struct parsed_request
{
	request_head head {};
	protocol version = protocol::v1_1;
	body_framing framing = body_framing::none;
	uint64_t content_length = 0;
	bool close = true;
};

struct parsed_response
{
	response_head head {};
	protocol version = protocol::v1_1;
	body_framing framing = body_framing::none;
	uint64_t content_length = 0;
	bool close = true;
};

[[nodiscard]] sys_expected<std::string> serialize_request(
	const request_head &head, const body_spec &spec, protocol version) noexcept;

[[nodiscard]] sys_expected<std::string> serialize_response(
	const response_head &head, const body_spec &spec, protocol version,
	bool close = true) noexcept;

[[nodiscard]] sys_expected<parsed_request> parse_request(
	std::string_view wire, const message_limits &limits) noexcept;

[[nodiscard]] sys_expected<parsed_response> parse_response(
	std::string_view wire, const message_limits &limits,
	bool head_request) noexcept;

[[nodiscard]] sys_expected<uint64_t> parse_chunk_size(
	std::string_view line) noexcept;

[[nodiscard]] std::string serialize_chunk(const_buffer buffer);
[[nodiscard]] std::string serialize_last_chunk(const fields &trailers);

} //namespace riwo::http_exp::detail::h1

#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_H1_WIRE_H
