// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_EXP_PROTOCOL_HANDSHAKE_H
#define RIWO_WEBSOCKET_EXP_PROTOCOL_HANDSHAKE_H

#include <riwo/http_exp/protocol.h>
#include <riwo/websocket_exp/protocol/types.h>
#include <riwo/websocket_exp/error.h>

namespace riwo::websocket_exp
{

struct opening_request
{
	std::string key {};
	std::vector<std::string> subprotocols {};
	std::vector<extension> extensions {};
};

struct opening_response
{
	optional<std::string> subprotocol {};
	std::vector<extension> extensions {};
};

[[nodiscard]] RIWO_WEBSOCKET_EXP_API
sys_expected<std::string> make_client_key(std::span<const std::byte,16> nonce) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_EXP_API
sys_expected<std::string> make_accept_key(std::string_view client_key) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_EXP_API
sys_expected<http_exp::fields> make_opening_request_headers (
	const opening_request &request, http_exp::protocol version = http_exp::protocol::v1_1
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_EXP_API
sys_expected<opening_request> parse_opening_request (
	const http_exp::method &method, http_exp::protocol version,
	const http_exp::fields &headers, bool extended_connect = false
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_EXP_API
sys_expected<http_exp::fields> make_opening_response_headers (
	const opening_request &request, const opening_response &response,
	http_exp::protocol version = http_exp::protocol::v1_1
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_EXP_API
sys_expected<opening_response> parse_opening_response(
	const http_exp::status &status, const http_exp::fields &headers, const opening_request &request,
	http_exp::protocol version = http_exp::protocol::v1_1
) noexcept;

} //namespace riwo::websocket_exp


#endif //RIWO_WEBSOCKET_EXP_PROTOCOL_HANDSHAKE_H
