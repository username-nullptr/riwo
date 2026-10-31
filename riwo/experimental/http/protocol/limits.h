// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_PROTOCOL_LIMITS_H
#define RIWO_EXP_HTTP_PROTOCOL_LIMITS_H

#include <riwo/experimental/http/global.h>

namespace riwo::experimental::http
{

struct message_limits
{
	size_t max_start_line_size = 8 * 1024;

	size_t max_field_name_size = 8 * 1024;
	size_t max_field_value_size = 16 * 1024;

	size_t max_header_list_size = 64 * 1024;
	size_t max_field_count = 100;

	optional<uint64_t> max_body_size {};

	uint64_t max_buffered_body = 8 * 1024 * 1024;
	uint64_t max_discard_body = 1024 * 1024;
};

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_PROTOCOL_LIMITS_H
