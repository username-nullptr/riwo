// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_H1_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_H1_SESSION_H

#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/transport/connection.h>

namespace riwo::http_exp::detail::h1
{

[[nodiscard]] std::shared_ptr<client_session>
make_client_session(asio::any_io_executor executor, protocol version,
	message_limits limits = {});

[[nodiscard]] std::shared_ptr<client_session>
make_client_session(asio::any_io_executor executor, protocol version,
	message_limits limits,
	std::shared_ptr<::riwo::http_exp::transport::connection> transport);

[[nodiscard]] std::shared_ptr<server_session>
make_server_session(asio::ip::tcp::socket socket,
	message_limits limits = {});

[[nodiscard]] std::shared_ptr<server_session>
make_server_session(
	std::shared_ptr<::riwo::http_exp::transport::connection> transport,
	protocol version, message_limits limits = {});

} //namespace riwo::http_exp::detail::h1

#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_H1_SESSION_H
