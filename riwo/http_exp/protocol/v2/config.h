// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_V2_CONFIG_H
#define RIWO_HTTP_EXP_PROTOCOL_V2_CONFIG_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

#if RIWO_HTTP_EXP_HTTP2_SUPPORT
struct v2_config
{
	uint32_t initial_stream_window = 65'535;
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

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_V2_CONFIG_H
