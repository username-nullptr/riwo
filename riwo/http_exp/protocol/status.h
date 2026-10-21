// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_PROTOCOL_STATUS_H
#define RIWO_HTTP_EXP_PROTOCOL_STATUS_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API status
{
public:
	enum well_known : uint16_t
	{
		// 1xx: Informational
		continue_upload = 100,
		switching_protocols = 101,
		processing = 102,
		early_hints = 103,

		// 2xx: Success
		ok = 200,
		created = 201,
		accepted = 202,
		non_authoritative_info = 203,
		no_content = 204,
		reset_content = 205,
		partial_content = 206,
		multi_status = 207,
		already_reported = 208,
		im_used = 226,

		// 3xx: Redirection
		multiple_choices = 300,
		moved_permanently = 301,
		found = 302,
		see_other = 303,
		not_modified = 304,
		use_proxy = 305, // Deprecated by RFC 9110.
		temporary_redirect = 307,
		permanent_redirect = 308,

		// 4xx: Client Error
		bad_request = 400,
		unauthorized = 401,
		payment_required = 402,
		forbidden = 403,
		not_found = 404,
		method_not_allowed = 405,
		not_acceptable = 406,
		proxy_auth_required = 407,
		request_timeout = 408,
		conflict = 409,
		gone = 410,
		length_required = 411,
		precondition_failed = 412,
		content_too_large = 413,
		uri_too_long = 414,
		unsupported_media_type = 415,
		range_not_satisfiable = 416,
		expectation_failed = 417,
		im_a_teapot = 418, // Reserved by RFC 9110; legacy text is retained.
		misdirected_request = 421,
		unprocessable_content = 422,
		locked = 423,
		failed_dependency = 424,
		too_early = 425,
		upgrade_required = 426,
		precondition_required = 428,
		too_many_requests = 429,
		headers_too_large = 431,
		legal_restriction = 451,

		// 5xx: Server Error
		internal_server_error = 500,
		not_implemented = 501,
		bad_gateway = 502,
		service_unavailable = 503,
		gateway_timeout = 504,
		unsupported_version = 505,
		variant_also_negotiates = 506,
		insufficient_storage = 507,
		loop_detected = 508,
		not_extended = 510, // Obsoleted RFC 2774 extension.
		network_auth_required = 511
	};

	status(well_known value) noexcept;
	explicit status(uint16_t code);

	[[nodiscard]] uint16_t code() const noexcept;
	[[nodiscard]] std::string_view text() const noexcept;
	[[nodiscard]] bool allows_body() const noexcept;

	operator uint16_t() const noexcept;
	friend bool operator==(status, status) noexcept = default;

private:
	uint16_t m_code;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_PROTOCOL_STATUS_H
