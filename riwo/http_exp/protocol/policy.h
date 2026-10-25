// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_POLICY_H
#define RIWO_HTTP_EXP_PROTOCOL_POLICY_H

#include <riwo/http_exp/protocol/version.h>

namespace riwo::http_exp
{

enum class protocol_requirement
{
	automatic,
	require_v1,
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	require_v2
#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
};

struct protocol_policy
{
	std::vector<protocol> preference {
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		protocol::v2,
#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
		protocol::v1_1
	};
	protocol_requirement requirement = protocol_requirement::automatic;
};

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_PROTOCOL_POLICY_H
