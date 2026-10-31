// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "syntax.h"
#include <riwo/core/utils/string_tools.h>

namespace riwo::experimental::http::detail
{

std::string_view trim_ows(std::string_view value) noexcept
{
	while( not value.empty() and (value.front() == ' ' or value.front() == '\t') )
		value.remove_prefix(1);
	while( not value.empty() and (value.back() == ' ' or value.back() == '\t') )
		value.remove_suffix(1);
	return value;
}

bool ascii_token_character(unsigned char value) noexcept
{
	return (value >= '0' and value <= '9') or
		   (value >= 'A' and value <= 'Z') or
		   (value >= 'a' and value <= 'z') or
		   std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(value)) != std::string_view::npos;
}

bool comma_list_contains(std::string_view value,std::string_view token) noexcept
{
	for(;;)
	{
		const auto comma = value.find(',');
		if( strtls::ascii_equal(trim_ows(value.substr(0, comma)), token, false) )
			return true;

		if( comma == std::string_view::npos )
			return false;

		value.remove_prefix(comma + 1);
	}
}

} //namespace riwo::experimental::http::detail
