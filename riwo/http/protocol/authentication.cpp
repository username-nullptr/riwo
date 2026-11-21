// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "authentication.h"
#include "detail/syntax.h"
#include <riwo/crypto/base64.h>

namespace riwo::http { namespace
{

[[nodiscard]] optional<std::pair<std::string_view,std::string_view>>
split_scheme(std::string_view value) noexcept
{
	value = detail::trim_ows(value);
	const auto space = value.find_first_of(" \t");

	if( space == std::string_view::npos )
		return {};

	auto scheme = value.substr(0, space);
	value = detail::trim_ows(value.substr(space));

	if( value.empty() )
		return {};

	return std::pair(scheme, value);
}

} //namespace

std::string make_basic_authorization(std::string_view username, std::string_view password)
{
	if( username.find(':') != std::string_view::npos )
		invalid_argument::loc_throw("HTTP Basic username cannot contain ':'");

	return "Basic " + crypto::base64_encode (
		std::string(username) + ':' + std::string(password)
	);
}

std::string make_bearer_authorization(std::string_view token)
{
	if( token.empty() or token.find_first_of("\r\n") != std::string_view::npos )
		invalid_argument::loc_throw("invalid HTTP Bearer token");
	return "Bearer " + std::string(token);
}

optional<basic_credentials> parse_basic_authorization(std::string_view value) noexcept
try
{
	const auto split = split_scheme(value);
	if( not split or not strtls::ascii_equal(split->first, "Basic", false) )
		return {};

	auto decoded = crypto::base64_decode(split->second);
	if( not decoded )
		return {};

	const auto colon = decoded->find(':');
	if( colon == std::string::npos )
		return {};

	return basic_credentials {
		.username = decoded->substr(0, colon),
		.password = decoded->substr(colon + 1)
	};
}
catch(...) {
	return {};
}

optional<std::string> parse_bearer_authorization(std::string_view value) noexcept
try
{
	const auto split = split_scheme(value);
	if( not split or not strtls::ascii_equal(split->first, "Bearer", false) )
		return {};

	if( split->second.find_first_of("\r\n") != std::string_view::npos )
		return {};

	return std::string(split->second);
}
catch(...) {
	return {};
}

void set_basic_authorization
(fields &headers, std::string_view username, std::string_view password, bool proxy)
{
	headers.set(proxy ? "Proxy-Authorization" : "Authorization",
		make_basic_authorization(username, password)
	);
}

void set_bearer_authorization(fields &headers, std::string_view token, bool proxy)
{
	headers.set(proxy ? "Proxy-Authorization" : "Authorization",
		make_bearer_authorization(token)
	);
}

} //namespace riwo::http
