// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_SERVER_CONFIG_H
#define RIWO_HTTP_EXP_SERVER_CONFIG_H

#include <riwo/http_exp/client/config.h>

namespace riwo::http_exp
{

struct h1_config
{
	size_t max_pipeline_depth = 1;
};

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
struct h2_config
{
	uint32_t initial_stream_window = 65535;
	uint32_t initial_connection_window = 1024 * 1024;
	uint32_t max_concurrent_streams = 100;
	uint32_t max_frame_size = 16 * 1024;
	size_t max_header_list_size = 64 * 1024;
	size_t max_dynamic_table_size = 4 * 1024;
	size_t inbound_stream_buffer = 256 * 1024;
	size_t outbound_stream_buffer = 256 * 1024;
	size_t inbound_connection_buffer = 8 * 1024 * 1024;
	size_t outbound_connection_buffer = 8 * 1024 * 1024;
	bool enable_extended_connect = false;
};
#endif

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
