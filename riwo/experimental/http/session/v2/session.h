// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SESSION_V2_SESSION_H
#define RIWO_EXP_HTTP_SESSION_V2_SESSION_H

#include <riwo/experimental/http/session/client_session.h>
#include <riwo/experimental/http/session/server_session.h>
#include <riwo/experimental/http/transport/connection.h>
#include <riwo/experimental/http/protocol/v2/config.h>
#include <riwo/experimental/http/protocol/limits.h>
#include <riwo/experimental/http/timeout.h>

#if RIWO_EXP_HTTP_HTTP2_SUPPORT

namespace riwo::experimental::http::v2
{

// Keep the protocol engine independent from the client and server facades.
// The owning layer copies only the values required by an HTTP/2 Session.
struct client_session_config
{
	timeout_policy timeout {};
	message_limits reply_limits {};
	v2_config h2 {};
};

struct server_session_config
{
	timeout_policy timeout {};
	message_limits request_limits {};
	v2_config h2 {};
};

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<client_session_ptr> make_client_session (
	connection_ptr transport, const client_session_config &config,
	const asio::ip::tcp::endpoint& remote = {},
	const asio::ip::tcp::endpoint& local = {}
);

[[nodiscard]] RIWO_DECL_HIDDEN
sys_expected<server_session_ptr> make_server_session (
	connection_ptr transport, const server_session_config &config,
	std::string initial_input = {}
);

} //namespace riwo::experimental::http::v2


#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
#endif //RIWO_EXP_HTTP_SESSION_V2_SESSION_H
