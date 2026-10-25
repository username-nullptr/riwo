// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_SESSION_H

#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/server/config.h>

namespace riwo::http_exp
{
class connection;
}

namespace riwo::http_exp
{
class session_stream;
}

namespace riwo::http_exp::detail
{

using server_accept_handler_t = std::function<void(
	error_code,std::shared_ptr<session_stream>,request_head)>;

class RIWO_HTTP_EXP_API server_connection
{
public:
	server_connection() = default;
	virtual ~server_connection() = default;
	virtual void cancel() noexcept = 0;
	[[nodiscard]] virtual bool closed() const noexcept = 0;
};

using server_connection_ptr_t = std::shared_ptr<server_connection>;

[[nodiscard]] RIWO_HTTP_EXP_API server_connection_ptr_t accept_h1(
	asio::ip::tcp::socket socket,
	message_limits limits, timeout_policy timeouts,
	server_accept_handler_t handler);

[[nodiscard]] RIWO_HTTP_EXP_API server_connection_ptr_t accept_h1(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	message_limits limits, timeout_policy timeouts,
	server_accept_handler_t handler, std::string initial_input = {});

// Selects a cleartext HTTP/1.x or HTTP/2 server Session according to the
// frozen server policy.  The HTTP/2 client preface is inspected without
// losing bytes: a mismatch is replayed to the HTTP/1 parser.
[[nodiscard]] RIWO_HTTP_EXP_API server_connection_ptr_t accept_server(
	asio::ip::tcp::socket socket, server_config config,
	server_accept_handler_t handler);

[[nodiscard]] RIWO_HTTP_EXP_API server_connection_ptr_t accept_server(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	protocol selected_protocol, server_config config,
	server_accept_handler_t handler);

[[nodiscard]] RIWO_HTTP_EXP_API error_code validate_server_config(
	const server_config &config) noexcept;

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_SESSION_H
