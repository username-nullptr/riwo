// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/client/connector.h>

namespace riwo::http::detail
{

std::string proxy_authority(const connect_target &target)
{
	const bool ipv6 =
		target.host.find(':') != std::string::npos and
		not target.host.starts_with('[');

	return (ipv6 ? "[" + target.host + "]" : target.host) +
		":" + std::to_string(target.port);
}

error_code validate_proxy_tunnel(const connect_target &target, const proxy_tunnel &proxy) noexcept
{
	if( target.host.empty() or target.port == 0 or proxy.host.empty() or proxy.port == 0 )
		return make_system_error_code(std::errc::invalid_argument);

	if( proxy.type != proxy_tunnel_type::http_connect and proxy.type != proxy_tunnel_type::socks5 )
		return make_system_error_code(std::errc::invalid_argument);

	if( proxy.authorization and
		(proxy.authorization->find('\r') != std::string::npos or
		 proxy.authorization->find('\n') != std::string::npos) )
		return make_system_error_code(std::errc::invalid_argument);

	if( proxy.type == proxy_tunnel_type::socks5 and
		(target.host.size() > 255 or (proxy.username and proxy.username->size() > 255) or
			(proxy.password and proxy.password->size() > 255)) )
		return make_system_error_code(std::errc::invalid_argument);
	return {};
}

} //namespace riwo::http::detail
