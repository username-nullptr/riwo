// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "container.h"

namespace riwo
{

parameter_map::iterator parameter_map::find(std::string_view key)
{
	return std::ranges::find(*this, key, [](const auto &pair) {
		return return_reference(pair.first);
	});
}

parameter_map::const_iterator parameter_map::find(std::string_view key) const
{
	return std::ranges::find(*this, key, [](const auto &pair) {
		return return_reference(pair.first);
	});
}

value &parameter_map::operator[](std::string_view key)
{
	auto it = find(key);
	if( it == end() )
	{
		emplace_back(key, value{});
		it = std::prev(end());
	}
	return it->second;
}

value &parameter_map::operator[](std::string &&key)
{
	auto it = find(key);
	if( it == end() )
	{
		emplace_back(std::move(key), value{});
		it = std::prev(end());
	}
	return it->second;
}

const value &parameter_map::operator[](std::string_view key) const
{
	auto it = find(key);
	if( it == end() )
	{
		out_of_range::loc_throw (
			"riwo::parameter_map: key not found"
		);
	}
	return it->second;
}

} //namespace riwo
