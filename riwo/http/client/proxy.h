// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_PROXY_H
#define RIWO_HTTP_CLIENT_PROXY_H

#include <riwo/http/client/connector.h>
#include <riwo/core/url.h>

namespace riwo::http
{

struct use_global_proxy_t {};
struct no_proxy_t {};

constexpr use_global_proxy_t use_global_proxy {};
constexpr no_proxy_t no_proxy {};

using proxy_t = std::variant <
	url, proxy_tunnel, use_global_proxy_t, no_proxy_t
>;

} //namespace riwo::http


#endif //RIWO_HTTP_CLIENT_PROXY_H
