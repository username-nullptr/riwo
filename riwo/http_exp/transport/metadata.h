// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_METADATA_H
#define RIWO_HTTP_EXP_TRANSPORT_METADATA_H

#include <riwo/http_exp/global.h>
#include <unordered_map>

namespace riwo::http_exp
{

struct transport_capabilities
{
	bool synchronous = false;
	bool cancellable = true;
	bool secure = false;
};

struct transport_metadata
{
	optional<std::string> local_endpoint {};
	optional<std::string> remote_endpoint {};
	bool tls = false;
	optional<std::string> alpn {};
	std::unordered_map<std::string,std::string> attributes {};
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_TRANSPORT_METADATA_H
