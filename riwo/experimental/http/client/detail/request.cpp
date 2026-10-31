// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/experimental/http/client/request.h>

namespace riwo::experimental::http
{

request::impl::impl(experimental::http::method request_method, riwo::url request_url) :
	m_head {.method = std::move(request_method), .url = std::move(request_url)}
{

}

} //namespace riwo::experimental::http
