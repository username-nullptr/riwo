// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http_exp/client/request.h>

namespace riwo::http_exp
{

request::impl::impl(http_exp::method request_method, riwo::url request_url) :
	m_head {.method = std::move(request_method), .url = std::move(request_url)}
{

}

} //namespace riwo::http_exp
