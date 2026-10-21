// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_ERROR_H
#define RIWO_HTTP_EXP_ERROR_H

#include <riwo/http_exp/protocol/version.h>

namespace riwo::http_exp
{

#define RIWO_HTTP_EXP_ERRC_TABLE \
X_MACRO(invalid_method,             1,  "Invalid HTTP method") \
X_MACRO(invalid_status,             2,  "Invalid HTTP status") \
X_MACRO(invalid_target,             3,  "Invalid HTTP request target") \
X_MACRO(invalid_field_name,         4,  "Invalid HTTP field name") \
X_MACRO(invalid_field_value,        5,  "Invalid HTTP field value") \
X_MACRO(invalid_trailer,            6,  "Invalid HTTP trailer") \
X_MACRO(header_limit_exceeded,      7,  "HTTP header limit exceeded") \
X_MACRO(body_length_mismatch,       8,  "HTTP body length mismatch") \
X_MACRO(body_not_allowed,           9,  "HTTP body is not allowed") \
X_MACRO(request_body_closed,        10, "HTTP request body is closed") \
X_MACRO(operation_in_progress,      11, "HTTP operation already in progress") \
X_MACRO(operation_not_permitted,    12, "HTTP operation not permitted") \
X_MACRO(buffer_limit_exceeded,      13, "HTTP buffer limit exceeded") \
X_MACRO(negotiation_failed,         14, "HTTP protocol negotiation failed") \
X_MACRO(connection_protocol_error,  15, "HTTP connection protocol error") \
X_MACRO(stream_protocol_error,      16, "HTTP stream protocol error") \
X_MACRO(operation_not_supported,    17, "HTTP operation not supported") \
X_MACRO(invalid_cookie,              18, "Invalid HTTP cookie") \
X_MACRO(unexpected_eof,              19, "Unexpected end of HTTP input") \
X_MACRO(invalid_protocol_action,     20, "Invalid HTTP protocol action")

enum class errc
{
#define X_MACRO(e,v,d) e = (v),
	RIWO_HTTP_EXP_ERRC_TABLE
#undef X_MACRO
};

enum class error_scope
{
	transport,
	negotiation,
	connection_protocol,
	stream_protocol,
	timeout,
	cancelled,
	application
};

enum class request_delivery
{
	not_started,
	definitely_not_processed,
	possibly_processed
};

struct error_info
{
	error_code code {};
	error_scope scope = error_scope::application;
	optional<protocol> selected_protocol {};
	optional<int32_t> stream_id {};
	optional<uint32_t> wire_error_code {};
	request_delivery delivery = request_delivery::not_started;
	bool retryable = false;
};

[[nodiscard]] RIWO_HTTP_EXP_API
const error_category_t &error_category() noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API
error_code make_error_code(errc value) noexcept;

} //namespace riwo::http_exp

template <>
struct std::is_error_code_enum<riwo::http_exp::errc> : true_type {};

#endif //RIWO_HTTP_EXP_ERROR_H
