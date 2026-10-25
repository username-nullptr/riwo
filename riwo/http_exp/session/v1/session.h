// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SESSION_V1_SESSION_H
#define RIWO_HTTP_EXP_SESSION_V1_SESSION_H

#include <riwo/http_exp/session/client_session.h>
#include <riwo/http_exp/session/server_session.h>
#include <riwo/http_exp/protocol/limits.h>
#include <riwo/http_exp/transport/connection.h>
#include <riwo/http_exp/timeout.h>

namespace riwo::http_exp::v1
{

[[nodiscard]] RIWO_HTTP_EXP_API
client_session_ptr make_client_session (
	protocol version, const message_limits &limits = {}, const timeout_policy &timeouts = {},
	bool absolute_form = false, asio::any_io_executor executor = get_executor()
);

[[nodiscard]] RIWO_HTTP_EXP_API
client_session_ptr make_client_session (
	asio::any_io_executor executor, protocol version, const message_limits &limits = {},
	const timeout_policy &timeouts = {}, bool absolute_form = false
);

[[nodiscard]] RIWO_HTTP_EXP_API
client_session_ptr make_client_session (
	protocol version, const message_limits &limits, connection_ptr transport,
	const timeout_policy &timeouts = {}, bool absolute_form = false,
	asio::any_io_executor executor = get_executor()
);

[[nodiscard]] RIWO_HTTP_EXP_API
client_session_ptr make_client_session (
	asio::any_io_executor executor, protocol version, const message_limits &limits,
	connection_ptr transport, const timeout_policy &timeouts = {},
	bool absolute_form = false
);

[[nodiscard]] RIWO_HTTP_EXP_API
server_session_ptr make_server_session (
	asio::ip::tcp::socket socket, const message_limits &limits = {},
	const timeout_policy &timeouts = {}
);

[[nodiscard]] RIWO_HTTP_EXP_API
server_session_ptr make_server_session (
	connection_ptr transport, protocol version, const message_limits &limits = {},
	const timeout_policy &timeouts = {}, std::string initial_input = {},
	std::function<void(bool)> release_handler = {}
);

} //namespace riwo::http_exp::v1


#endif //RIWO_HTTP_EXP_SESSION_V1_SESSION_H
