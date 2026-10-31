// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SESSION_V1_SESSION_H
#define RIWO_EXP_HTTP_SESSION_V1_SESSION_H

#include <riwo/experimental/http/session/client_session.h>
#include <riwo/experimental/http/session/server_session.h>
#include <riwo/experimental/http/protocol/limits.h>
#include <riwo/experimental/http/transport/connection.h>
#include <riwo/experimental/http/timeout.h>

namespace riwo::experimental::http::v1
{

[[nodiscard]] RIWO_DECL_HIDDEN
client_session_ptr make_client_session (
	protocol version, const message_limits &limits = {}, const timeout_policy &timeouts = {},
	bool absolute_form = false, asio::any_io_executor executor = get_executor()
);

[[nodiscard]] RIWO_DECL_HIDDEN
client_session_ptr make_client_session (
	asio::any_io_executor executor, protocol version, const message_limits &limits = {},
	const timeout_policy &timeouts = {}, bool absolute_form = false
);

[[nodiscard]] RIWO_DECL_HIDDEN
client_session_ptr make_client_session (
	protocol version, const message_limits &limits, connection_ptr transport,
	const timeout_policy &timeouts = {}, bool absolute_form = false,
	asio::any_io_executor executor = get_executor()
);

[[nodiscard]] RIWO_DECL_HIDDEN
client_session_ptr make_client_session (
	asio::any_io_executor executor, protocol version, const message_limits &limits,
	connection_ptr transport, const timeout_policy &timeouts = {},
	bool absolute_form = false
);

[[nodiscard]] RIWO_DECL_HIDDEN
server_session_ptr make_server_session (
	asio::ip::tcp::socket socket, const message_limits &limits = {},
	const timeout_policy &timeouts = {}
);

[[nodiscard]] RIWO_DECL_HIDDEN
server_session_ptr make_server_session (
	connection_ptr transport, protocol version, const message_limits &limits = {},
	const timeout_policy &timeouts = {}, std::string initial_input = {},
	std::function<void(bool)> release_handler = {}
);

} //namespace riwo::experimental::http::v1


#endif //RIWO_EXP_HTTP_SESSION_V1_SESSION_H
