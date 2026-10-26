// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXAMPLES_HTTP_EXP_COMMON_H
#define RIWO_EXAMPLES_HTTP_EXP_COMMON_H

#include <riwo/http_exp.h>

namespace riwo::http_exp::example
{

[[nodiscard]] inline protocol_requirement parse_protocol(std::string_view value)
{
	if( value == "auto" )
		return protocol_requirement::automatic;
	if( value == "http1" )
		return protocol_requirement::v1;
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	if( value == "http2" )
		return protocol_requirement::v2;
#endif
	throw std::invalid_argument (
		"protocol must be auto, http1"
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
		" or http2"
#endif
	);
}

[[nodiscard]] inline std::string_view protocol_text(protocol value) noexcept
{
	switch(value)
	{
	case protocol::v1_0: return "HTTP/1.0";
	case protocol::v1_1: return "HTTP/1.1";
#if RIWO_HTTP_EXP_HTTP2_SUPPORT
	case protocol::v2: return "HTTP/2";
#endif
	}
	return "unknown";
}

[[nodiscard]] inline std::string_view body_text(std::span<const std::byte> body) noexcept
{
	if( body.empty() )
		return {};
	return {
		reinterpret_cast<const char*>(body.data()),
		body.size()
	};
}

} //namespace riwo::http_exp::example


#endif //RIWO_EXAMPLES_HTTP_EXP_COMMON_H
