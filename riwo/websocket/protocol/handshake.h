// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_PROTOCOL_HANDSHAKE_H
#define RIWO_WEBSOCKET_PROTOCOL_HANDSHAKE_H

#include <riwo/http/protocol.h>
#include <riwo/websocket/protocol/types.h>
#include <riwo/websocket/error.h>

namespace riwo::websocket
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

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<std::string> make_client_key(std::span<const std::byte,16> nonce) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<std::string> make_accept_key(std::string_view client_key) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<http::fields> make_opening_request_headers (
	const opening_request &request, http::protocol version = http::protocol::v1_1
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<opening_request> parse_opening_request (
	const http::method &method, http::protocol version,
	const http::fields &headers, bool extended_connect = false
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<http::fields> make_opening_response_headers (
	const opening_request &request, const opening_response &response,
	http::protocol version = http::protocol::v1_1
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<opening_response> parse_opening_response(
	const http::status &status, const http::fields &headers, const opening_request &request,
	http::protocol version = http::protocol::v1_1
) noexcept;

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_PROTOCOL_HANDSHAKE_H
