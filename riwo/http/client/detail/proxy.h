// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_CLIENT_DETAIL_PROXY_H
#define RIWO_HTTP_CLIENT_DETAIL_PROXY_H

#include <riwo/http/client/proxy.h>

namespace riwo::http::detail
{

struct resolved_proxy
{
	optional<url> forward {};
	optional<proxy_tunnel> tunnel {};
	optional<std::string> authorization {};
};

[[nodiscard]] RIWO_HTTP_API sys_expected<resolved_proxy>
resolve_proxy(const url &target, const proxy_t &setting) noexcept;

// Internal entry point for protocol adapters such as WebSocket. The caller
// owns the environment-variable precedence; parsing and NO_PROXY handling stay
// shared with the HTTP client.
[[nodiscard]] RIWO_HTTP_API sys_expected<resolved_proxy>
resolve_global_proxy(const url &target,
	std::initializer_list<std::string_view> variable_names
) noexcept;

} //namespace riwo::http::detail


#endif //RIWO_HTTP_CLIENT_DETAIL_PROXY_H
