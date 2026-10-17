// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "request.h"

namespace riwo::http_exp
{

server_request::server_request(http_exp::method request_method,
	std::string target, fields request_headers) :
	method_(std::move(request_method)), target_(std::move(target)),
	headers_(std::move(request_headers))
{
}

} //namespace riwo::http_exp
