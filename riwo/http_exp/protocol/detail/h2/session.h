// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_DETAIL_H2_SESSION_H
#define RIWO_HTTP_EXP_PROTOCOL_DETAIL_H2_SESSION_H

#include <riwo/http_exp/client/config.h>
#include <riwo/http_exp/protocol/detail/session.h>
#include <riwo/http_exp/transport/connection.h>

#if RIWO_HTTP_EXP_HTTP2_SUPPORT

namespace riwo::http_exp::detail::h2
{

[[nodiscard]] sys_expected<std::shared_ptr<client_session>>
make_client_session(std::shared_ptr<::riwo::http_exp::connection> transport,
	client_config config, asio::ip::tcp::endpoint remote = {},
	asio::ip::tcp::endpoint local = {});

} //namespace riwo::http_exp::detail::h2

#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT

#endif //RIWO_HTTP_EXP_PROTOCOL_DETAIL_H2_SESSION_H
