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
	require_http1,
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	require_http2
#endif
};

struct protocol_policy
{
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	std::vector<protocol> preference {protocol::v2, protocol::v1_1};
#else
	std::vector<protocol> preference {protocol::v1_1};
#endif
	protocol_requirement requirement = protocol_requirement::automatic;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_POLICY_H
