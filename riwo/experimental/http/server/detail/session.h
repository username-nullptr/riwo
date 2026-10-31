// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_DETAIL_SESSION_H
#define RIWO_EXP_HTTP_SERVER_DETAIL_SESSION_H

#include <riwo/experimental/http/session/session_stream.h>
#include <riwo/experimental/http/protocol/request_head.h>
#include <riwo/experimental/http/server/config.h>

namespace riwo::experimental::http
{

class connection;
class session_stream;

namespace detail
{

using server_accept_handler_t = std::function <
	void(error_code,session_stream_ptr,request_head)
>;

class
#ifdef __GNUC__
 RIWO_EXP_HTTP_API
#else //__GNUC__
 RIWO_DECL_HIDDEN
#endif //__GNUC__
server_connection
{
	RIWO_DISABLE_COPY_MOVE(server_connection)

public:
	server_connection() = default;
	virtual ~server_connection() = default;

	[[nodiscard]] virtual bool closed() const noexcept = 0;
	virtual void cancel() noexcept = 0;
};

using server_connection_ptr = std::shared_ptr<server_connection>;

[[nodiscard]] RIWO_DECL_HIDDEN server_connection_ptr accept_h1 (
	asio::ip::tcp::socket socket, const message_limits &limits, const timeout_policy &timeouts,
	server_accept_handler_t handler
);
[[nodiscard]] RIWO_DECL_HIDDEN server_connection_ptr accept_h1 (
	std::shared_ptr<connection> transport, const message_limits &limits, const timeout_policy &timeouts,
	server_accept_handler_t handler, std::string initial_input = {}
);

// Selects a cleartext HTTP/1.x or HTTP/2 server Session according to the
// frozen server policy.  The HTTP/2 client preface is inspected without
// losing bytes: a mismatch is replayed to the HTTP/1 parser.
[[nodiscard]] RIWO_EXP_HTTP_API server_connection_ptr accept_server (
	asio::ip::tcp::socket socket, server_config config, server_accept_handler_t handler
);
[[nodiscard]] RIWO_EXP_HTTP_API server_connection_ptr accept_server (
	std::shared_ptr<connection> transport, protocol selected_protocol, const server_config &config,
	server_accept_handler_t handler
);

[[nodiscard]] RIWO_EXP_HTTP_API
error_code validate_server_config(const server_config &config) noexcept;

}} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_SERVER_DETAIL_SESSION_H
