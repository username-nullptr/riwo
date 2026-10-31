// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_CONFIG_H
#define RIWO_EXP_HTTP_SERVER_CONFIG_H

#include <riwo/experimental/http/protocol/v2/config.h>
#include <riwo/experimental/http/protocol/limits.h>
#include <riwo/experimental/http/protocol/policy.h>

#include <riwo/experimental/http/server/app_session_manager.h>
#include <riwo/experimental/http/timeout.h>

namespace riwo::experimental::http
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
#if RIWO_EXP_HTTP_HTTP2_SUPPORT
	v2_config v2 {};
#endif //RIWO_EXP_HTTP_HTTP2_SUPPORT
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_SERVER_CONFIG_H
