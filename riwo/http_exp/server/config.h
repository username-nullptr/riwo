// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_CONFIG_H
#define RIWO_HTTP_EXP_SERVER_CONFIG_H

#include <riwo/http_exp/protocol/limits.h>
#include <riwo/http_exp/protocol/policy.h>
#include <riwo/http_exp/protocol/v2/config.h>
#include <riwo/http_exp/timeout.h>

namespace riwo::http_exp
{

struct h1_config
{
	size_t max_pipeline_depth = 1;
};

struct server_config
{
	protocol_policy protocols {};
	timeout_policy timeout {};
	message_limits request_limits {};
	h1_config h1 {};
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	h2_config h2 {};
#endif
	std::filesystem::path resource_root {};
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_SERVER_CONFIG_H
