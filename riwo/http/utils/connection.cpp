// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "connection.h"
#include <charconv>

namespace riwo::http
{

bool endpoint::from_string(std::string_view text)
{
	std::string_view address_text {};
	std::string_view port_text {};
	if( text.empty() )
		return false;

	if( text.front() == '[' )
	{
		auto close = text.find(']');
		if( close == std::string_view::npos or close + 1 >= text.size() or
			text[close + 1] != ':' )
			return false;
		address_text = text.substr(1, close - 1);
		port_text = text.substr(close + 2);
	}
	else
	{
		auto colon = text.rfind(':');
		if( colon == std::string_view::npos or text.find(':') != colon )
			return false;
		address_text = text.substr(0, colon);
		port_text = text.substr(colon + 1);
	}

	uint32_t parsed_port = 0;
	auto [ptr, parse_error] = std::from_chars(port_text.data(),
		port_text.data() + port_text.size(), parsed_port
	);
	if( parse_error != std::errc{} or
		ptr != port_text.data() + port_text.size() or
		parsed_port > std::numeric_limits<uint16_t>::max() )
		return false;

	error_code error {};
	auto parsed_address = asio::ip::make_address(address_text, error);
	if( error )
		return false;

	address = std::move(parsed_address);
	port = static_cast<uint16_t>(parsed_port);
	return true;
}

std::string endpoint::to_string() const
{
	if( address.is_v6() )
		return std::format("[{}]:{}", address.to_string(), port);
	return std::format("{}:{}", address.to_string(), port);
}

} //namespace riwo::http
