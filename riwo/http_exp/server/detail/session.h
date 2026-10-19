// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_DETAIL_SESSION_H
#define RIWO_HTTP_EXP_SERVER_DETAIL_SESSION_H

#include <riwo/http_exp/protocol/request_head.h>
#include <riwo/http_exp/server/config.h>

namespace riwo::http_exp::detail
{

class stream;

using server_accept_handler = asio::any_completion_handler<void(
	error_code,std::shared_ptr<stream>,request_head)>;

RIWO_HTTP_EXP_API void accept_h1(asio::ip::tcp::socket socket,
	message_limits limits, server_accept_handler handler);

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_SERVER_DETAIL_SESSION_H
