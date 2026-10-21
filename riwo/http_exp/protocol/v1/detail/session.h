// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V1_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_V1_DETAIL_SESSION_H

#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/transport/connection.h>

namespace riwo::http_exp::v1::detail
{

[[nodiscard]] std::shared_ptr<::riwo::http_exp::detail::client_session>
make_client_session(asio::any_io_executor executor, protocol version,
	message_limits limits = {}, timeout_policy timeouts = {},
	bool absolute_form = false);

[[nodiscard]] std::shared_ptr<::riwo::http_exp::detail::client_session>
make_client_session(asio::any_io_executor executor, protocol version,
	message_limits limits,
	std::shared_ptr<::riwo::http_exp::connection> transport,
	timeout_policy timeouts = {}, bool absolute_form = false);

[[nodiscard]] std::shared_ptr<::riwo::http_exp::detail::server_session>
make_server_session(asio::ip::tcp::socket socket,
	message_limits limits = {}, timeout_policy timeouts = {});

[[nodiscard]] std::shared_ptr<::riwo::http_exp::detail::server_session>
make_server_session(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	protocol version, message_limits limits = {},
	timeout_policy timeouts = {});

} //namespace riwo::http_exp::v1::detail

#endif //RIWO_HTTP_EXP_PROTOCOL_V1_DETAIL_SESSION_H
