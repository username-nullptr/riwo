// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SESSION_V2_SESSION_H
#define RIWO_HTTP_EXP_SESSION_V2_SESSION_H

#include <riwo/http_exp/session/client_session.h>
#include <riwo/http_exp/session/server_session.h>
#include <riwo/http_exp/transport/connection.h>
#include <riwo/http_exp/protocol/v2/config.h>
#include <riwo/http_exp/protocol/limits.h>
#include <riwo/http_exp/timeout.h>

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

namespace riwo::http_exp::v2
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

[[nodiscard]] RIWO_HTTP_EXP_API
sys_expected<client_session_ptr> make_client_session (
	connection_ptr transport, const client_session_config &config,
	const asio::ip::tcp::endpoint& remote = {},
	const asio::ip::tcp::endpoint& local = {}
);

[[nodiscard]] RIWO_HTTP_EXP_API
sys_expected<server_session_ptr> make_server_session (
	connection_ptr transport, const server_session_config &config,
	std::string initial_input = {}
);

} //namespace riwo::http_exp::v2


#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
#endif //RIWO_HTTP_EXP_SESSION_V2_SESSION_H
