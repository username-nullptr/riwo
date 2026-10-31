// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_V1_DETAIL_WIRE_H
#define RIWO_EXP_HTTP_PROTOCOL_V1_DETAIL_WIRE_H

#include <riwo/experimental/http/protocol/body_spec.h>
#include <riwo/experimental/http/error.h>

#include <riwo/experimental/http/protocol/limits.h>
#include <riwo/experimental/http/protocol/request_head.h>
#include <riwo/experimental/http/protocol/response_head.h>

namespace riwo::experimental::http::v1::detail
{

enum class body_framing {
	none, content_length, chunked, until_eof
};

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

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<std::string> serialize_request (
	const request_head &head, const body_spec &spec, protocol version,
	bool absolute_form = false
) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<std::string> serialize_response(
	const response_head &head, const body_spec &spec, protocol version,
	bool close = true
) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<parsed_request> parse_request (
	std::string_view wire, const message_limits &limits
) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<parsed_response> parse_response (
	std::string_view wire, const message_limits &limits, bool head_request
) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<uint64_t> parse_chunk_size(std::string_view line) noexcept;

[[nodiscard]] RIWO_DECL_HIDDEN
std::string serialize_chunk_header(size_t size);

[[nodiscard]] RIWO_DECL_HIDDEN
std::string serialize_last_chunk(const fields &trailers);

} //namespace riwo::experimental::http::v1::detail


#endif //RIWO_EXP_HTTP_PROTOCOL_V1_DETAIL_WIRE_H
