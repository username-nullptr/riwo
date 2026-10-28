// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_CONFIG_H
#define RIWO_HTTP_EXP_SERVER_CONFIG_H

#include <riwo/http_exp/protocol/v2/config.h>
#include <riwo/http_exp/protocol/limits.h>
#include <riwo/http_exp/protocol/policy.h>

#include <riwo/http_exp/server/app_session_manager.h>
#include <riwo/http_exp/timeout.h>

namespace riwo::http_exp
{

struct v1_config {
	size_t max_pipeline_depth = 1;
};

struct default_response_config
{
	// When disabled, an unfinished exchange is completed with an empty 204.
	bool enabled = true;
	bool html = true;
};

struct server_compression_config
{
	bool enabled = gzip_available_v;
	uint64_t minimum_size = 1024;
};

struct server_config
{
	protocol_policy protocols {};
	timeout_policy timeout {};
	message_limits request_limits {};

	app_session_config sessions {};
	default_response_config default_responses {};

	server_compression_config compression {};
	std::filesystem::path resource_root {};

	v1_config v1 {};
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	v2_config v2 {};
#endif //RIWO_HTTP_EXP_HTTP2_SUPPORT
};

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_SERVER_CONFIG_H
