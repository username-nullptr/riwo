// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_TRANSPORT_METADATA_H
#define RIWO_EXP_HTTP_TRANSPORT_METADATA_H

#include <riwo/experimental/http/global.h>
#include <riwo/core/value.h>
#include <unordered_map>

namespace riwo::experimental::http
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

	std::unordered_map<std::string,value> attributes {};
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_TRANSPORT_METADATA_H
