// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_SESSION_H

#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/server/config.h>
#include <riwo/http_exp/server/detail/session.h>
#include <riwo/http_exp/transport/connection.h>

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

namespace riwo::http_exp::v2::detail
{

[[nodiscard]] sys_expected<std::shared_ptr<
	::riwo::http_exp::detail::client_session>>
make_client_session(std::shared_ptr<::riwo::http_exp::connection> transport,
	client_config config, asio::ip::tcp::endpoint remote = {},
	asio::ip::tcp::endpoint local = {});

[[nodiscard]] sys_expected<std::shared_ptr<
	::riwo::http_exp::detail::server_session>>
make_server_session(std::shared_ptr<::riwo::http_exp::connection> transport,
	server_config config, std::string initial_input = {});

[[nodiscard]] ::riwo::http_exp::detail::server_connection_ptr_t accept_h2(
	std::shared_ptr<::riwo::http_exp::connection> transport,
	server_config config, std::string initial_input,
	::riwo::http_exp::detail::server_accept_handler_t handler);

} //namespace riwo::http_exp::v2::detail

#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT

#endif //RIWO_HTTP_EXP_PROTOCOL_V2_DETAIL_SESSION_H
