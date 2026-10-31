// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_POLICY_H
#define RIWO_EXP_HTTP_PROTOCOL_POLICY_H

#include <riwo/experimental/http/version.h>

namespace riwo::experimental::http
{

enum class protocol_requirement
{
	automatic, v1,
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	v2
#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
};

struct protocol_policy
{
	std::vector<protocol> preference {
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
		protocol::v2,
#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
		protocol::v1_1
	};
	protocol_requirement requirement = protocol_requirement::automatic;
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_POLICY_H
