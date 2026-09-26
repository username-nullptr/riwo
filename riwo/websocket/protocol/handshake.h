// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_WEBSOCKET_PROTOCOL_HANDSHAKE_H
#define RIWO_WEBSOCKET_PROTOCOL_HANDSHAKE_H

#include <riwo/http/protocol/types.h>
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
sys_expected<http::headers> make_opening_request_headers(const opening_request &request) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<opening_request> parse_opening_request (
	http::method_enum method, http::version_enum version, const http::headers &headers
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<http::headers> make_opening_response_headers (
	const opening_request &request, const opening_response &response
) noexcept;

[[nodiscard]] RIWO_WEBSOCKET_API
sys_expected<opening_response> parse_opening_response (
	http::status_enum status, const http::headers &headers, const opening_request &request
) noexcept;

} //namespace riwo::websocket


#endif //RIWO_WEBSOCKET_PROTOCOL_HANDSHAKE_H
