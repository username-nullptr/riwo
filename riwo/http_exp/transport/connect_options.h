// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_CONNECT_OPTIONS_H
#define RIWO_HTTP_EXP_TRANSPORT_CONNECT_OPTIONS_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

struct proxy_policy
{
	optional<riwo::url> forward_proxy {};
};

struct tls_policy
{
	bool verify_peer = true;
	optional<std::filesystem::path> certificate_authority_file {};
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_CONNECT_OPTIONS_H
