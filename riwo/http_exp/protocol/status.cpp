// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "status.h"

namespace riwo::http_exp
{

class status::impl
{
public:
	explicit impl(uint16_t value) : m_code(value) {}

	uint16_t m_code;
};

status::status(well_known value) noexcept :
	m_impl(std::make_shared<impl>(static_cast<uint16_t>(value)))
{
}

status::status(uint16_t code)
{
	if( code < 100 or code > 599 )
		throw std::invalid_argument("HTTP status must be in the range 100..599");
	m_impl = std::make_shared<impl>(code);
}

uint16_t status::code() const noexcept {
	return m_impl->m_code;
}

std::string_view status::text() const noexcept
{
	switch( code() )
	{
	case 100: return "Continue";
	case 101: return "Switching Protocols";
	case 102: return "Processing";
	case 103: return "Early Hints";
	case 200: return "OK";
	case 201: return "Created";
	case 202: return "Accepted";
	case 203: return "Non-Authoritative Information";
	case 204: return "No Content";
	case 205: return "Reset Content";
	case 206: return "Partial Content";
	case 207: return "Multi-Status";
	case 208: return "Already Reported";
	case 226: return "IM Used";
	case 300: return "Multiple Choices";
	case 301: return "Moved Permanently";
	case 302: return "Found";
	case 303: return "See Other";
	case 304: return "Not Modified";
	case 305: return "Use Proxy";
	case 307: return "Temporary Redirect";
	case 308: return "Permanent Redirect";
	case 400: return "Bad Request";
	case 401: return "Unauthorized";
	case 402: return "Payment Required";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 406: return "Not Acceptable";
	case 407: return "Proxy Authentication Required";
	case 408: return "Request Timeout";
	case 409: return "Conflict";
	case 410: return "Gone";
	case 411: return "Length Required";
	case 412: return "Precondition Failed";
	case 413: return "Content Too Large";
	case 414: return "URI Too Long";
	case 415: return "Unsupported Media Type";
	case 416: return "Range Not Satisfiable";
	case 417: return "Expectation Failed";
	case 418: return "I'm a teapot";
	case 421: return "Misdirected Request";
	case 422: return "Unprocessable Content";
	case 423: return "Locked";
	case 424: return "Failed Dependency";
	case 425: return "Too Early";
	case 426: return "Upgrade Required";
	case 428: return "Precondition Required";
	case 429: return "Too Many Requests";
	case 431: return "Request Header Fields Too Large";
	case 451: return "Unavailable For Legal Reasons";
	case 500: return "Internal Server Error";
	case 501: return "Not Implemented";
	case 502: return "Bad Gateway";
	case 503: return "Service Unavailable";
	case 504: return "Gateway Timeout";
	case 505: return "HTTP Version Not Supported";
	case 506: return "Variant Also Negotiates";
	case 507: return "Insufficient Storage";
	case 508: return "Loop Detected";
	case 510: return "Not Extended";
	case 511: return "Network Authentication Required";
	default: return "Status";
	}
}

bool status::allows_body() const noexcept
{
	const auto value = code();
	return value >= 200 and value != no_content and
		value != reset_content and value != not_modified;
}

bool operator==(status lhs, status::well_known rhs) noexcept {
	return lhs.code() == static_cast<uint16_t>(rhs);
}

bool operator==(status::well_known lhs, status rhs) noexcept {
	return static_cast<uint16_t>(lhs) == rhs.code();
}

bool operator==(status lhs, status rhs) noexcept {
	return lhs.code() == rhs.code();
}

status::operator well_known() const noexcept
{
	return static_cast<well_known>(code());
}

status::operator uint16_t() const noexcept
{
	return code();
}

} //namespace riwo::http_exp
