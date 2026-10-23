// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "authentication.h"
#include <riwo/core/algorithm/base64.h>

namespace riwo::http_exp
{

namespace
{

[[nodiscard]] bool ascii_iequal(std::string_view lhs,
	std::string_view rhs) noexcept
{
	return lhs.size() == rhs.size() and std::ranges::equal(lhs, rhs,
		[](unsigned char left, unsigned char right) {
			return std::tolower(left) == std::tolower(right);
		});
}

[[nodiscard]] optional<std::pair<std::string_view,std::string_view>>
split_scheme(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	const auto space = value.find_first_of(" \t");
	if( space == std::string_view::npos ) return {};
	auto scheme = value.substr(0, space);
	value.remove_prefix(space);
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	if( value.empty() ) return {};
	return std::pair(scheme, value);
}

[[nodiscard]] optional<std::string> decode_base64(std::string_view value)
{
	if( value.empty() or value.size() % 4 != 0 ) return {};
	auto digit = [](unsigned char ch) -> int {
		if( ch >= 'A' and ch <= 'Z' ) return ch - 'A';
		if( ch >= 'a' and ch <= 'z' ) return ch - 'a' + 26;
		if( ch >= '0' and ch <= '9' ) return ch - '0' + 52;
		if( ch == '+' ) return 62;
		if( ch == '/' ) return 63;
		return -1;
	};
	std::string result;
	result.reserve(value.size() / 4 * 3);
	for( size_t offset = 0; offset < value.size(); offset += 4 )
	{
		const auto first = digit(static_cast<unsigned char>(value[offset]));
		const auto second = digit(static_cast<unsigned char>(value[offset + 1]));
		const auto third = value[offset + 2] == '=' ? -2 :
			digit(static_cast<unsigned char>(value[offset + 2]));
		const auto fourth = value[offset + 3] == '=' ? -2 :
			digit(static_cast<unsigned char>(value[offset + 3]));
		if( first < 0 or second < 0 or third == -1 or fourth == -1 ) return {};
		if( third == -2 and fourth != -2 ) return {};
		if( (third == -2 or fourth == -2) and offset + 4 != value.size() )
			return {};
		const uint32_t bits = (static_cast<uint32_t>(first) << 18U) |
			(static_cast<uint32_t>(second) << 12U) |
			(static_cast<uint32_t>(std::max(third, 0)) << 6U) |
			static_cast<uint32_t>(std::max(fourth, 0));
		result.push_back(static_cast<char>(bits >> 16U));
		if( third != -2 ) result.push_back(static_cast<char>(bits >> 8U));
		if( fourth != -2 ) result.push_back(static_cast<char>(bits));
	}
	return result;
}

} //namespace

std::string make_basic_authorization(std::string_view username,
	std::string_view password)
{
	if( username.find(':') != std::string_view::npos )
		throw std::invalid_argument("HTTP Basic username cannot contain ':'");
	return "Basic " + riwo::base64_encode(
		std::string(username) + ':' + std::string(password));
}

std::string make_bearer_authorization(std::string_view token)
{
	if( token.empty() or token.find_first_of("\r\n") != std::string_view::npos )
		throw std::invalid_argument("invalid HTTP Bearer token");
	return "Bearer " + std::string(token);
}

optional<basic_credentials> parse_basic_authorization(
	std::string_view value) noexcept
{
	try
	{
		const auto split = split_scheme(value);
		if( not split or not ascii_iequal(split->first, "Basic") ) return {};
		auto decoded = decode_base64(split->second);
		if( not decoded ) return {};
		const auto colon = decoded->find(':');
		if( colon == std::string::npos ) return {};
		return basic_credentials {decoded->substr(0, colon),
			decoded->substr(colon + 1)};
	}
	catch(...) { return {}; }
}

optional<std::string> parse_bearer_authorization(
	std::string_view value) noexcept
{
	try
	{
		const auto split = split_scheme(value);
		if( not split or not ascii_iequal(split->first, "Bearer") ) return {};
		if( split->second.find_first_of("\r\n") != std::string_view::npos )
			return {};
		return std::string(split->second);
	}
	catch(...) { return {}; }
}

void set_basic_authorization(fields &headers, std::string_view username,
	std::string_view password, bool proxy)
{
	headers.set(proxy ? "Proxy-Authorization" : "Authorization",
		make_basic_authorization(username, password));
}

void set_bearer_authorization(fields &headers, std::string_view token,
	bool proxy)
{
	headers.set(proxy ? "Proxy-Authorization" : "Authorization",
		make_bearer_authorization(token));
}

} //namespace riwo::http_exp
