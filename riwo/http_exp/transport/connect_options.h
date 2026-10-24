// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_CONNECT_OPTIONS_H
#define RIWO_HTTP_EXP_TRANSPORT_CONNECT_OPTIONS_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

enum class proxy_type {
	automatic, http, socks5
};

struct proxy_policy
{
	optional<url> forward_proxy {};
	proxy_type type = proxy_type::automatic;

	bool use_environment = false;
	std::vector<std::string> no_proxy {};

	optional<std::string> username {};
	optional<std::string> password {};
	optional<std::string> authorization {};
};

struct tls_policy
{
	optional<std::filesystem::path> certificate_authority_file {};
	bool verify_peer = true;
};

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_TRANSPORT_CONNECT_OPTIONS_H
